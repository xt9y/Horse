#include "Models/Compression/ZstdHuffman.hpp"

#include "Models/Compression/ZstdBits.hpp"
#include "Models/Compression/ZstdFse.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Models::Compression::ZstdInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

unsigned int highBit(std::uint32_t value)
{
    unsigned int result = 0u;
    while (value > 1u) {
        value >>= 1u;
        ++result;
    }
    return result;
}

bool powerOfTwo(std::uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

bool decodeCompressedWeights(
    const std::uint8_t *data,
    std::size_t size,
    std::vector<std::uint8_t> *weights,
    std::string *error)
{
    if (!data || !weights || size == 0u) return fail(error, "truncated Zstd Huffman weight stream");
    FseTable table;
    std::size_t table_bytes = 0u;
    if (!parseFseTable(data, size, 15u, 6u, &table, &table_bytes, error)) return false;
    if (table_bytes >= size) return fail(error, "Zstd Huffman weight stream has no FSE payload");

    ReverseBits bits;
    if (!bits.reset(data + table_bytes, size - table_bytes, error)) return false;
    FseState state1;
    FseState state2;
    if (!initializeFseState(table, &bits, &state1, error) ||
        !initializeFseState(table, &bits, &state2, error))
        return false;

    weights->clear();
    weights->reserve(255u);
    for (;;) {
        if (weights->size() >= 255u) return fail(error, "Zstd Huffman weight stream produces too many symbols");
        std::uint16_t symbol = 0u;
        if (!fseSymbol(state1, &symbol, error)) return false;
        if (symbol > 15u) return fail(error, "Zstd Huffman weight exceeds representable range");
        weights->push_back(static_cast<std::uint8_t>(symbol));

        bool overflow = false;
        if (!updateFseStatePadded(&state1, &bits, &overflow, error)) return false;
        if (overflow) {
            if (weights->size() >= 255u) return fail(error, "Zstd Huffman weight stream produces too many symbols");
            if (!fseSymbol(state2, &symbol, error)) return false;
            if (symbol > 15u) return fail(error, "Zstd Huffman weight exceeds representable range");
            weights->push_back(static_cast<std::uint8_t>(symbol));
            break;
        }

        if (weights->size() >= 255u) return fail(error, "Zstd Huffman weight stream produces too many symbols");
        if (!fseSymbol(state2, &symbol, error)) return false;
        if (symbol > 15u) return fail(error, "Zstd Huffman weight exceeds representable range");
        weights->push_back(static_cast<std::uint8_t>(symbol));
        if (!updateFseStatePadded(&state2, &bits, &overflow, error)) return false;
        if (overflow) {
            if (weights->size() >= 255u) return fail(error, "Zstd Huffman weight stream produces too many symbols");
            if (!fseSymbol(state1, &symbol, error)) return false;
            if (symbol > 15u) return fail(error, "Zstd Huffman weight exceeds representable range");
            weights->push_back(static_cast<std::uint8_t>(symbol));
            break;
        }
    }
    if (!bits.empty()) return fail(error, "Zstd Huffman FSE weight stream has unread bits");
    return weights->size() >= 2u;
}

bool completeWeights(std::vector<std::uint8_t> *weights, std::uint8_t *maximum_bits, std::string *error)
{
    if (!weights || !maximum_bits || weights->empty() || weights->size() >= 256u)
        return fail(error, "invalid Zstd Huffman weight list");

    std::uint32_t total = 0u;
    std::size_t nonzero = 0u;
    for (const std::uint8_t weight : *weights) {
        if (weight > 11u) return fail(error, "Zstd Huffman weight exceeds maximum code length");
        if (weight == 0u) continue;
        total += std::uint32_t{1u} << (weight - 1u);
        ++nonzero;
    }
    if (total == 0u) return fail(error, "Zstd Huffman tree contains no symbols");

    const unsigned int table_log = highBit(total) + 1u;
    if (table_log > 11u) return fail(error, "Zstd Huffman tree depth exceeds 11 bits");
    const std::uint32_t full = std::uint32_t{1u} << table_log;
    if (total >= full) return fail(error, "Zstd Huffman weights do not leave room for implied symbol");
    const std::uint32_t remaining = full - total;
    if (!powerOfTwo(remaining)) return fail(error, "Zstd Huffman implied weight is not a power of two");
    const std::uint8_t implied = static_cast<std::uint8_t>(highBit(remaining) + 1u);
    if (implied == 0u || implied > table_log) return fail(error, "invalid Zstd Huffman implied weight");
    weights->push_back(implied);
    ++nonzero;
    if (nonzero < 2u) return fail(error, "Zstd Huffman tree requires at least two symbols");
    *maximum_bits = static_cast<std::uint8_t>(table_log);
    return true;
}

bool buildTree(
    const std::vector<std::uint8_t>& weights,
    std::uint8_t maximum_bits,
    HuffmanTable *table,
    std::string *error)
{
    if (!table || maximum_bits == 0u || maximum_bits > 11u)
        return fail(error, "invalid Zstd Huffman table parameters");

    struct SymbolWeight {
        std::uint16_t symbol = 0u;
        std::uint8_t weight = 0u;
    };
    std::vector<SymbolWeight> present;
    present.reserve(weights.size());
    for (std::size_t symbol = 0u; symbol < weights.size(); ++symbol) {
        if (weights[symbol] == 0u) continue;
        present.push_back({static_cast<std::uint16_t>(symbol), weights[symbol]});
    }
    std::stable_sort(
        present.begin(),
        present.end(),
        [](const SymbolWeight& a, const SymbolWeight& b) {
            if (a.weight != b.weight) return a.weight < b.weight;
            return a.symbol < b.symbol;
        }
    );

    table->clear();
    table->maximum_bits = maximum_bits;
    table->nodes.push_back({});
    std::uint32_t code = 0u;
    unsigned int current_bits = maximum_bits;

    for (const SymbolWeight& item : present) {
        const unsigned int bits = static_cast<unsigned int>(maximum_bits) + 1u - item.weight;
        if (bits == 0u || bits > maximum_bits || bits > current_bits)
            return fail(error, "invalid Zstd Huffman canonical code length ordering");
        if (bits < current_bits) {
            code >>= current_bits - bits;
            current_bits = bits;
        }
        if (code >= (std::uint32_t{1u} << bits)) return fail(error, "Zstd Huffman canonical code overflows");

        std::int16_t node = 0;
        for (unsigned int depth = 0u; depth < bits; ++depth) {
            if (table->nodes[static_cast<std::size_t>(node)].symbol >= 0)
                return fail(error, "Zstd Huffman prefix collision");
            const unsigned int shift = bits - depth - 1u;
            const std::size_t branch = (code >> shift) & 1u;
            std::int16_t& child = table->nodes[static_cast<std::size_t>(node)].child[branch];
            if (child < 0) {
                if (table->nodes.size() >= static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()))
                    return fail(error, "Zstd Huffman tree is too large");
                child = static_cast<std::int16_t>(table->nodes.size());
                table->nodes.push_back({});
            }
            node = child;
        }
        HuffmanNode& leaf = table->nodes[static_cast<std::size_t>(node)];
        if (leaf.symbol >= 0 || leaf.child[0] >= 0 || leaf.child[1] >= 0)
            return fail(error, "Zstd Huffman duplicate/prefix code");
        leaf.symbol = static_cast<std::int16_t>(item.symbol);
        ++code;
    }
    return table->valid();
}

} // namespace

bool parseHuffmanTable(
    const std::uint8_t *data,
    std::size_t size,
    HuffmanTable *table,
    std::size_t *consumed,
    std::string *error)
{
    if (!data || !table || !consumed || size == 0u) return fail(error, "truncated Zstd Huffman tree description");
    const std::uint8_t header = data[0];
    std::vector<std::uint8_t> weights;
    std::size_t payload_bytes = 0u;

    if (header >= 128u) {
        const std::size_t count = static_cast<std::size_t>(header - 127u);
        payload_bytes = (count + 1u) / 2u;
        if (payload_bytes > size - 1u) return fail(error, "truncated direct Zstd Huffman weights");
        weights.resize(count);
        for (std::size_t index = 0u; index < count; ++index) {
            const std::uint8_t packed = data[1u + index / 2u];
            weights[index] = (index & 1u) == 0u ? packed >> 4u : packed & 15u;
        }
    } else {
        payload_bytes = header;
        if (payload_bytes == 0u || payload_bytes > size - 1u)
            return fail(error, "invalid FSE-compressed Zstd Huffman weight size");
        if (!decodeCompressedWeights(data + 1u, payload_bytes, &weights, error)) return false;
    }

    std::uint8_t maximum_bits = 0u;
    if (!completeWeights(&weights, &maximum_bits, error)) return false;
    if (!buildTree(weights, maximum_bits, table, error)) return false;
    *consumed = 1u + payload_bytes;
    return true;
}

bool decodeHuffmanStream(
    const HuffmanTable& table,
    const std::uint8_t *data,
    std::size_t size,
    std::size_t regenerated_size,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!output || !table.valid()) return fail(error, "invalid Zstd Huffman decode arguments");
    output->clear();
    output->reserve(regenerated_size);
    if (regenerated_size == 0u) {
        if (size != 0u) return fail(error, "nonempty Zstd Huffman stream for zero literals");
        return true;
    }

    ReverseBits bits;
    if (!bits.reset(data, size, error)) return false;
    for (std::size_t index = 0u; index < regenerated_size; ++index) {
        std::int16_t node = 0;
        unsigned int depth = 0u;
        while (table.nodes[static_cast<std::size_t>(node)].symbol < 0) {
            if (depth >= table.maximum_bits) return fail(error, "Zstd Huffman code exceeds tree depth");
            std::uint32_t bit = 0u;
            if (!bits.read(1u, &bit, error)) return false;
            node = table.nodes[static_cast<std::size_t>(node)].child[bit & 1u];
            if (node < 0 || static_cast<std::size_t>(node) >= table.nodes.size())
                return fail(error, "Zstd Huffman stream references an invalid prefix");
            ++depth;
        }
        const std::int16_t symbol = table.nodes[static_cast<std::size_t>(node)].symbol;
        if (symbol < 0 || symbol > 255) return fail(error, "Zstd Huffman symbol exceeds byte range");
        output->push_back(static_cast<std::uint8_t>(symbol));
    }
    if (!bits.empty()) return fail(error, "Zstd Huffman stream contains trailing bits");
    return true;
}

} // namespace Models::Compression::ZstdInternal
