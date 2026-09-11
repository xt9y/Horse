#include "Models/Images/Etc1s.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Models::Images::Etc1s {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint16_t u16le(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u)
    );
}

std::uint32_t u32le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

class Bits {
public:
    Bits() = default;
    Bits(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    bool read(unsigned int count, std::uint32_t *out, std::string *error)
    {
        if (!out || count > 32u) return fail(error, "invalid BasisLZ bit read");
        if (bit_ > size_ * 8u || count > size_ * 8u - bit_)
            return fail(error, "truncated BasisLZ bitstream");
        std::uint32_t value = 0u;
        for (unsigned int i = 0u; i < count; ++i) {
            const std::size_t position = bit_ + i;
            value |= static_cast<std::uint32_t>(
                (data_[position >> 3u] >> static_cast<unsigned int>(position & 7u)) & 1u
            ) << i;
        }
        bit_ += count;
        *out = value;
        return true;
    }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t bit_ = 0u;
};

std::uint32_t reverseBits(std::uint32_t value, unsigned int count)
{
    std::uint32_t result = 0u;
    for (unsigned int i = 0u; i < count; ++i) {
        result = (result << 1u) | (value & 1u);
        value >>= 1u;
    }
    return result;
}

struct HuffmanNode {
    std::array<int, 2> child {{-1, -1}};
    int symbol = -1;
};

struct Huffman {
    std::vector<HuffmanNode> nodes;
    int single = -1;
    std::size_t symbol_count = 0u;

    bool valid() const { return single >= 0 || !nodes.empty(); }
};

bool buildHuffman(const std::vector<std::uint8_t>& lengths, Huffman *table, std::string *error)
{
    if (!table || lengths.empty()) return fail(error, "empty BasisLZ Huffman code lengths");
    table->nodes.clear();
    table->single = -1;
    table->symbol_count = lengths.size();

    std::array<std::uint32_t, 17> counts{};
    std::size_t used = 0u;
    int only = -1;
    unsigned int maximum = 0u;
    for (std::size_t symbol = 0u; symbol < lengths.size(); ++symbol) {
        const std::uint8_t length = lengths[symbol];
        if (length > 16u) return fail(error, "BasisLZ Huffman code length exceeds 16 bits");
        if (length == 0u) continue;
        ++counts[length];
        ++used;
        only = static_cast<int>(symbol);
        maximum = std::max<unsigned int>(maximum, length);
    }
    if (used == 0u) return fail(error, "BasisLZ Huffman table has no symbols");
    if (used == 1u) {
        table->single = only;
        return true;
    }

    std::int64_t left = 1;
    for (unsigned int bits_count = 1u; bits_count <= 16u; ++bits_count) {
        left = (left << 1) - static_cast<std::int64_t>(counts[bits_count]);
        if (left < 0) return fail(error, "oversubscribed BasisLZ Huffman table");
    }

    std::array<std::uint32_t, 17> next{};
    std::uint32_t code = 0u;
    for (unsigned int length = 1u; length <= 16u; ++length) {
        code = (code + counts[length - 1u]) << 1u;
        next[length] = code;
    }

    table->nodes.push_back({});
    for (std::size_t symbol = 0u; symbol < lengths.size(); ++symbol) {
        const unsigned int length = lengths[symbol];
        if (length == 0u) continue;
        const std::uint32_t canonical = next[length]++;
        if (length < 32u && canonical >= (std::uint32_t{1u} << length))
            return fail(error, "invalid BasisLZ canonical Huffman code");
        const std::uint32_t stream_code = reverseBits(canonical, length);
        int node = 0;
        for (unsigned int depth = 0u; depth < length; ++depth) {
            if (table->nodes[static_cast<std::size_t>(node)].symbol >= 0)
                return fail(error, "BasisLZ Huffman prefix collision");
            const unsigned int branch = (stream_code >> depth) & 1u;
            int& child = table->nodes[static_cast<std::size_t>(node)].child[branch];
            if (child < 0) {
                if (table->nodes.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    return fail(error, "BasisLZ Huffman tree is too large");
                child = static_cast<int>(table->nodes.size());
                table->nodes.push_back({});
            }
            node = child;
        }
        HuffmanNode& leaf = table->nodes[static_cast<std::size_t>(node)];
        if (leaf.symbol >= 0 || leaf.child[0] >= 0 || leaf.child[1] >= 0)
            return fail(error, "BasisLZ Huffman duplicate/prefix code");
        leaf.symbol = static_cast<int>(symbol);
    }
    return true;
}

bool decodeHuffman(Bits *bits, const Huffman& table, std::uint32_t *symbol, std::string *error)
{
    if (!bits || !symbol || !table.valid()) return fail(error, "invalid BasisLZ Huffman decode state");
    if (table.single >= 0) {
        *symbol = static_cast<std::uint32_t>(table.single);
        return true;
    }
    int node = 0;
    for (unsigned int depth = 0u; depth < 16u; ++depth) {
        if (node < 0 || static_cast<std::size_t>(node) >= table.nodes.size())
            return fail(error, "BasisLZ Huffman stream references invalid node");
        const HuffmanNode& current = table.nodes[static_cast<std::size_t>(node)];
        if (current.symbol >= 0) {
            *symbol = static_cast<std::uint32_t>(current.symbol);
            return true;
        }
        std::uint32_t branch = 0u;
        if (!bits->read(1u, &branch, error)) return false;
        node = current.child[branch & 1u];
    }
    if (node >= 0 && static_cast<std::size_t>(node) < table.nodes.size() &&
        table.nodes[static_cast<std::size_t>(node)].symbol >= 0) {
        *symbol = static_cast<std::uint32_t>(table.nodes[static_cast<std::size_t>(node)].symbol);
        return true;
    }
    return fail(error, "BasisLZ Huffman code exceeds 16 bits");
}

bool readHuffmanTable(Bits *bits, Huffman *table, std::string *error)
{
    if (!bits || !table) return fail(error, "invalid BasisLZ Huffman table request");
    std::uint32_t total = 0u;
    std::uint32_t code_count = 0u;
    if (!bits->read(14u, &total, error) || !bits->read(5u, &code_count, error)) return false;
    if (total == 0u || total > 16383u || code_count == 0u || code_count > 21u)
        return fail(error, "invalid BasisLZ compressed Huffman table header");

    static constexpr std::array<std::uint8_t, 21> order {{
        17u,18u,19u,20u,0u,8u,7u,9u,6u,10u,5u,11u,4u,12u,3u,13u,2u,14u,1u,15u,16u,
    }};
    std::vector<std::uint8_t> code_lengths(21u, 0u);
    for (std::size_t i = 0u; i < code_count; ++i) {
        std::uint32_t length = 0u;
        if (!bits->read(3u, &length, error)) return false;
        code_lengths[order[i]] = static_cast<std::uint8_t>(length);
    }
    Huffman length_table;
    if (!buildHuffman(code_lengths, &length_table, error)) return false;

    std::vector<std::uint8_t> lengths(total, 0u);
    std::size_t cursor = 0u;
    std::uint8_t previous = 0u;
    while (cursor < total) {
        std::uint32_t symbol = 0u;
        if (!decodeHuffman(bits, length_table, &symbol, error)) return false;
        if (symbol <= 16u) {
            lengths[cursor++] = static_cast<std::uint8_t>(symbol);
            if (symbol != 0u) previous = static_cast<std::uint8_t>(symbol);
            continue;
        }

        std::size_t run = 0u;
        std::uint8_t value = 0u;
        if (symbol == 17u) {
            std::uint32_t extra = 0u;
            if (!bits->read(3u, &extra, error)) return false;
            run = static_cast<std::size_t>(extra) + 3u;
        } else if (symbol == 18u) {
            std::uint32_t extra = 0u;
            if (!bits->read(7u, &extra, error)) return false;
            run = static_cast<std::size_t>(extra) + 11u;
        } else if (symbol == 19u) {
            if (cursor == 0u || previous == 0u) return fail(error, "invalid BasisLZ repeat-previous code length");
            std::uint32_t extra = 0u;
            if (!bits->read(2u, &extra, error)) return false;
            run = static_cast<std::size_t>(extra) + 3u;
            value = previous;
        } else if (symbol == 20u) {
            if (cursor == 0u || previous == 0u) return fail(error, "invalid BasisLZ long repeat-previous code length");
            std::uint32_t extra = 0u;
            if (!bits->read(7u, &extra, error)) return false;
            run = static_cast<std::size_t>(extra) + 7u;
            value = previous;
        } else {
            return fail(error, "invalid BasisLZ code-length symbol");
        }
        if (run > static_cast<std::size_t>(total) - cursor)
            return fail(error, "BasisLZ code-length run exceeds table size");
        std::fill_n(lengths.begin() + static_cast<std::ptrdiff_t>(cursor), run, value);
        cursor += run;
    }
    return buildHuffman(lengths, table, error);
}

struct Endpoint {
    std::uint8_t intensity = 0u;
    std::uint8_t r = 0u;
    std::uint8_t g = 0u;
    std::uint8_t b = 0u;
};

using Selector = std::array<std::uint8_t, 4>;

bool decodeEndpoints(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::vector<Endpoint> *endpoints,
    std::string *error)
{
    if (!data || !endpoints || count == 0u) return fail(error, "invalid ETC1S endpoint codebook");
    Bits bits(data, size);
    std::array<Huffman, 3> color_models;
    Huffman intensity_model;
    if (!readHuffmanTable(&bits, &color_models[0], error) ||
        !readHuffmanTable(&bits, &color_models[1], error) ||
        !readHuffmanTable(&bits, &color_models[2], error) ||
        !readHuffmanTable(&bits, &intensity_model, error))
        return false;
    std::uint32_t grayscale = 0u;
    if (!bits.read(1u, &grayscale, error)) return false;

    endpoints->resize(count);
    std::uint32_t previous_intensity = 0u;
    std::array<std::uint32_t, 3> previous {{16u, 16u, 16u}};
    for (std::size_t i = 0u; i < count; ++i) {
        std::uint32_t delta = 0u;
        if (!decodeHuffman(&bits, intensity_model, &delta, error)) return false;
        const std::uint32_t intensity = (previous_intensity + delta) & 7u;
        previous_intensity = intensity;
        Endpoint endpoint;
        endpoint.intensity = static_cast<std::uint8_t>(intensity);

        const unsigned int channels = grayscale != 0u ? 1u : 3u;
        std::array<std::uint8_t, 3> decoded{};
        for (unsigned int channel_index = 0u; channel_index < channels; ++channel_index) {
            const std::uint32_t previous_value = previous[channel_index];
            const std::size_t model_index = previous_value <= 9u ? 0u : previous_value <= 21u ? 1u : 2u;
            if (!decodeHuffman(&bits, color_models[model_index], &delta, error)) return false;
            const std::uint32_t value = (previous_value + delta) & 31u;
            previous[channel_index] = value;
            decoded[channel_index] = static_cast<std::uint8_t>(value);
        }
        if (grayscale != 0u) {
            endpoint.r = endpoint.g = endpoint.b = decoded[0];
            previous[1] = previous[2] = previous[0];
        } else {
            endpoint.r = decoded[0];
            endpoint.g = decoded[1];
            endpoint.b = decoded[2];
        }
        (*endpoints)[i] = endpoint;
    }
    return true;
}

bool decodeSelectors(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::vector<Selector> *selectors,
    std::string *error)
{
    if (!data || !selectors || count == 0u) return fail(error, "invalid ETC1S selector codebook");
    Bits bits(data, size);
    std::uint32_t reserved = 0u;
    std::uint32_t uncompressed = 0u;
    if (!bits.read(2u, &reserved, error) || !bits.read(1u, &uncompressed, error)) return false;
    if (reserved != 0u) return fail(error, "ETC1S selector codebook reserved bits are nonzero");

    selectors->resize(count);
    if (uncompressed != 0u) {
        for (Selector& selector : *selectors) {
            for (std::uint8_t& row : selector) {
                std::uint32_t value = 0u;
                if (!bits.read(8u, &value, error)) return false;
                row = static_cast<std::uint8_t>(value);
            }
        }
        return true;
    }

    Huffman delta_model;
    if (!readHuffmanTable(&bits, &delta_model, error)) return false;
    for (std::uint8_t& row : (*selectors)[0]) {
        std::uint32_t value = 0u;
        if (!bits.read(8u, &value, error)) return false;
        row = static_cast<std::uint8_t>(value);
    }
    for (std::size_t i = 1u; i < count; ++i) {
        for (std::size_t row = 0u; row < 4u; ++row) {
            std::uint32_t delta = 0u;
            if (!decodeHuffman(&bits, delta_model, &delta, error)) return false;
            if (delta > 255u) return fail(error, "ETC1S selector XOR delta exceeds byte range");
            (*selectors)[i][row] = static_cast<std::uint8_t>((*selectors)[i - 1u][row] ^ delta);
        }
    }
    return true;
}

struct SliceTables {
    Huffman endpoint_prediction;
    Huffman endpoint_delta;
    Huffman selector;
    Huffman selector_history_rle;
    std::size_t selector_history_size = 0u;
};

bool decodeSliceTables(const std::uint8_t *data, std::size_t size, SliceTables *tables, std::string *error)
{
    if (!data || !tables) return fail(error, "invalid ETC1S slice tables");
    Bits bits(data, size);
    if (!readHuffmanTable(&bits, &tables->endpoint_prediction, error) ||
        !readHuffmanTable(&bits, &tables->endpoint_delta, error) ||
        !readHuffmanTable(&bits, &tables->selector, error) ||
        !readHuffmanTable(&bits, &tables->selector_history_rle, error))
        return false;
    std::uint32_t history = 0u;
    if (!bits.read(13u, &history, error)) return false;
    if (history > 64u) return fail(error, "ETC1S selector history exceeds 64 entries");
    tables->selector_history_size = history;
    return true;
}

bool decodeVlc(Bits *bits, unsigned int payload_bits, std::uint32_t *out, std::string *error)
{
    if (!bits || !out || (payload_bits != 4u && payload_bits != 7u))
        return fail(error, "invalid ETC1S VLC request");
    std::uint32_t value = 0u;
    unsigned int offset = 0u;
    const unsigned int chunk_bits = payload_bits + 1u;
    for (;;) {
        if (offset >= 32u) return fail(error, "ETC1S VLC value exceeds 32 bits");
        std::uint32_t chunk = 0u;
        if (!bits->read(chunk_bits, &chunk, error)) return false;
        value |= (chunk & ((std::uint32_t{1u} << payload_bits) - 1u)) << offset;
        if ((chunk & (std::uint32_t{1u} << payload_bits)) == 0u) break;
        offset += payload_bits;
    }
    *out = value;
    return true;
}

class ApproxMtf {
public:
    explicit ApproxMtf(std::size_t size) : values_(size, 0u), rover_(size / 2u) {}

    std::size_t size() const { return values_.size(); }
    std::uint32_t at(std::size_t index) const { return values_[index]; }

    void add(std::uint32_t value)
    {
        if (values_.empty()) return;
        values_[rover_++] = value;
        if (rover_ == values_.size()) rover_ = values_.size() / 2u;
    }

    void use(std::size_t index)
    {
        if (index == 0u) return;
        std::swap(values_[index / 2u], values_[index]);
    }

private:
    std::vector<std::uint32_t> values_;
    std::size_t rover_ = 0u;
};

struct BlockRef {
    std::uint32_t endpoint = 0u;
    std::uint32_t selector = 0u;
};

bool decodeSlice(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t blocks_x,
    std::size_t blocks_y,
    std::size_t endpoint_count,
    std::size_t selector_count,
    const SliceTables& tables,
    std::vector<BlockRef> *blocks,
    std::string *error)
{
    if (!data || !blocks || blocks_x == 0u || blocks_y == 0u || endpoint_count == 0u || selector_count == 0u)
        return fail(error, "invalid ETC1S slice arguments");
    if (blocks_x > std::numeric_limits<std::size_t>::max() / blocks_y)
        return fail(error, "ETC1S slice dimensions overflow");
    Bits bits(data, size);
    blocks->assign(blocks_x * blocks_y, {});

    constexpr std::uint32_t EndpointPredRepeat = 256u;
    constexpr std::uint32_t EndpointPredMinimumRepeat = 3u;
    constexpr std::uint32_t SelectorRleThreshold = 3u;
    constexpr std::uint32_t SelectorRleTotal = 64u;

    std::array<std::vector<std::uint32_t>, 2> endpoint_rows {
        std::vector<std::uint32_t>(blocks_x, 0u),
        std::vector<std::uint32_t>(blocks_x, 0u),
    };
    std::vector<std::uint8_t> stored_pred((blocks_x + 1u) / 2u, 0u);
    ApproxMtf history(tables.selector_history_size);
    const std::uint32_t history_first = static_cast<std::uint32_t>(selector_count);
    const std::uint32_t history_rle = history_first + static_cast<std::uint32_t>(tables.selector_history_size);

    std::uint32_t selector_rle_count = 0u;
    std::uint32_t current_pred_bits = 0u;
    std::uint32_t previous_pred_symbol = 0u;
    std::uint32_t pred_repeat_count = 0u;
    std::uint32_t previous_endpoint = 0u;

    for (std::size_t y = 0u; y < blocks_y; ++y) {
        const std::size_t current_row = y & 1u;
        for (std::size_t x = 0u; x < blocks_x; ++x) {
            if ((x & 1u) == 0u) {
                if ((y & 1u) == 0u) {
                    if (pred_repeat_count != 0u) {
                        --pred_repeat_count;
                        current_pred_bits = previous_pred_symbol;
                    } else {
                        if (!decodeHuffman(&bits, tables.endpoint_prediction, &current_pred_bits, error)) return false;
                        if (current_pred_bits == EndpointPredRepeat) {
                            std::uint32_t count = 0u;
                            if (!decodeVlc(&bits, 4u, &count, error)) return false;
                            pred_repeat_count = count + EndpointPredMinimumRepeat - 1u;
                            current_pred_bits = previous_pred_symbol;
                        } else {
                            if (current_pred_bits > 255u)
                                return fail(error, "ETC1S endpoint prediction symbol exceeds 8 bits");
                            previous_pred_symbol = current_pred_bits;
                        }
                    }
                    stored_pred[x >> 1u] = static_cast<std::uint8_t>(current_pred_bits >> 4u);
                } else {
                    current_pred_bits = stored_pred[x >> 1u];
                }
            }

            const std::uint32_t pred = current_pred_bits & 3u;
            current_pred_bits >>= 2u;
            std::uint32_t endpoint_index = 0u;
            if (pred == 0u) {
                if (x == 0u) return fail(error, "ETC1S left endpoint prediction on first column");
                endpoint_index = previous_endpoint;
            } else if (pred == 1u) {
                if (y == 0u) return fail(error, "ETC1S upper endpoint prediction on first row");
                endpoint_index = endpoint_rows[current_row ^ 1u][x];
            } else if (pred == 2u) {
                if (x == 0u || y == 0u) return fail(error, "ETC1S upper-left endpoint prediction at image edge");
                endpoint_index = endpoint_rows[current_row ^ 1u][x - 1u];
            } else {
                std::uint32_t delta = 0u;
                if (!decodeHuffman(&bits, tables.endpoint_delta, &delta, error)) return false;
                if (delta >= endpoint_count) return fail(error, "ETC1S endpoint delta exceeds codebook size");
                endpoint_index = previous_endpoint + delta;
                if (endpoint_index >= endpoint_count) endpoint_index -= static_cast<std::uint32_t>(endpoint_count);
            }
            if (endpoint_index >= endpoint_count) return fail(error, "ETC1S endpoint index exceeds codebook");
            endpoint_rows[current_row][x] = endpoint_index;
            previous_endpoint = endpoint_index;

            std::uint32_t selector_symbol = 0u;
            if (selector_rle_count != 0u) {
                --selector_rle_count;
                selector_symbol = history_first;
            } else {
                if (!decodeHuffman(&bits, tables.selector, &selector_symbol, error)) return false;
                if (selector_symbol == history_rle) {
                    if (tables.selector_history_size == 0u)
                        return fail(error, "ETC1S selector history RLE used with empty history");
                    std::uint32_t run_symbol = 0u;
                    if (!decodeHuffman(&bits, tables.selector_history_rle, &run_symbol, error)) return false;
                    if (run_symbol == SelectorRleTotal - 1u) {
                        std::uint32_t extra = 0u;
                        if (!decodeVlc(&bits, 7u, &extra, error)) return false;
                        selector_rle_count = extra + SelectorRleThreshold;
                    } else {
                        if (run_symbol >= SelectorRleTotal)
                            return fail(error, "ETC1S selector history RLE symbol exceeds range");
                        selector_rle_count = run_symbol + SelectorRleThreshold;
                    }
                    selector_symbol = history_first;
                    --selector_rle_count;
                }
            }

            std::uint32_t selector_index = 0u;
            if (selector_symbol >= selector_count) {
                if (tables.selector_history_size == 0u)
                    return fail(error, "ETC1S selector history reference used with empty history");
                const std::size_t history_index = static_cast<std::size_t>(selector_symbol - selector_count);
                if (history_index >= history.size()) return fail(error, "ETC1S selector history index exceeds buffer");
                selector_index = history.at(history_index);
                history.use(history_index);
            } else {
                selector_index = selector_symbol;
                history.add(selector_index);
            }
            if (selector_index >= selector_count) return fail(error, "ETC1S selector index exceeds codebook");
            (*blocks)[y * blocks_x + x] = {endpoint_index, selector_index};
        }
    }
    return true;
}

std::uint8_t expand5(std::uint8_t value)
{
    return static_cast<std::uint8_t>((value << 3u) | (value >> 2u));
}

int modifier(std::uint8_t table, std::uint8_t selector)
{
    static constexpr std::array<std::array<int, 4>, 8> values {{
        {{-8,-2,2,8}}, {{-17,-5,5,17}}, {{-29,-9,9,29}}, {{-42,-13,13,42}},
        {{-60,-18,18,60}}, {{-80,-24,24,80}}, {{-106,-33,33,106}}, {{-183,-47,47,183}},
    }};
    return values[table & 7u][selector & 3u];
}

std::uint8_t clampByte(int value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

void renderSlice(
    const std::vector<BlockRef>& blocks,
    const std::vector<Endpoint>& endpoints,
    const std::vector<Selector>& selectors,
    std::size_t blocks_x,
    int width,
    int height,
    std::vector<std::uint8_t> *rgba,
    bool alpha_only)
{
    for (std::size_t block_index = 0u; block_index < blocks.size(); ++block_index) {
        const std::size_t bx = block_index % blocks_x;
        const std::size_t by = block_index / blocks_x;
        const BlockRef ref = blocks[block_index];
        const Endpoint& endpoint = endpoints[ref.endpoint];
        const Selector& selector = selectors[ref.selector];
        const int base_r = expand5(endpoint.r);
        const int base_g = expand5(endpoint.g);
        const int base_b = expand5(endpoint.b);
        for (std::size_t y = 0u; y < 4u; ++y) {
            for (std::size_t x = 0u; x < 4u; ++x) {
                const std::size_t px = bx * 4u + x;
                const std::size_t py = by * 4u + y;
                if (px >= static_cast<std::size_t>(width) || py >= static_cast<std::size_t>(height)) continue;
                const std::uint8_t select = static_cast<std::uint8_t>((selector[y] >> (x * 2u)) & 3u);
                const int delta = modifier(endpoint.intensity, select);
                const std::size_t destination = (py * static_cast<std::size_t>(width) + px) * 4u;
                if (alpha_only) {
                    (*rgba)[destination + 3u] = clampByte(base_r + delta);
                } else {
                    (*rgba)[destination + 0u] = clampByte(base_r + delta);
                    (*rgba)[destination + 1u] = clampByte(base_g + delta);
                    (*rgba)[destination + 2u] = clampByte(base_b + delta);
                    (*rgba)[destination + 3u] = 255u;
                }
            }
        }
    }
}

struct ImageDesc {
    std::uint32_t flags = 0u;
    std::uint32_t rgb_offset = 0u;
    std::uint32_t rgb_length = 0u;
    std::uint32_t alpha_offset = 0u;
    std::uint32_t alpha_length = 0u;
};

bool checkedSection(
    std::size_t offset,
    std::size_t length,
    std::size_t size,
    const std::uint8_t **out,
    std::string *error)
{
    if (!out || offset > size || length > size - offset) return fail(error, "BasisLZ section exceeds bounds");
    *out = nullptr;
    return true;
}

} // namespace

bool decodeKtx2BaseLevel(
    const std::uint8_t *global_data,
    std::size_t global_size,
    std::size_t image_count,
    const std::uint8_t *level_data,
    std::size_t level_size,
    int width,
    int height,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!global_data || !level_data || !image || global_size < 20u || image_count == 0u || width <= 0 || height <= 0)
        return fail(error, "invalid BasisLZ KTX2 inputs");
    if (image_count > (global_size - 20u) / 20u) return fail(error, "BasisLZ image descriptors exceed global data");

    const std::size_t endpoint_count = u16le(global_data + 0u);
    const std::size_t selector_count = u16le(global_data + 2u);
    const std::size_t endpoint_length = u32le(global_data + 4u);
    const std::size_t selector_length = u32le(global_data + 8u);
    const std::size_t tables_length = u32le(global_data + 12u);
    const std::size_t extended_length = u32le(global_data + 16u);
    if (endpoint_count == 0u || selector_count == 0u) return fail(error, "BasisLZ codebooks are empty");
    if (extended_length != 0u) return fail(error, "ETC1S BasisLZ extended data must be empty");

    const std::size_t descriptors_bytes = image_count * 20u;
    const std::size_t payload_offset = 20u + descriptors_bytes;
    if (endpoint_length > global_size - payload_offset ||
        selector_length > global_size - payload_offset - endpoint_length ||
        tables_length > global_size - payload_offset - endpoint_length - selector_length ||
        extended_length > global_size - payload_offset - endpoint_length - selector_length - tables_length)
        return fail(error, "BasisLZ global codebook sections exceed global data");
    if (payload_offset + endpoint_length + selector_length + tables_length + extended_length != global_size)
        return fail(error, "BasisLZ global data has trailing or missing bytes");

    const std::uint8_t *descriptor = global_data + 20u;
    ImageDesc desc;
    desc.flags = u32le(descriptor + 0u);
    desc.rgb_offset = u32le(descriptor + 4u);
    desc.rgb_length = u32le(descriptor + 8u);
    desc.alpha_offset = u32le(descriptor + 12u);
    desc.alpha_length = u32le(descriptor + 16u);
    if ((desc.flags & 0x02u) != 0u) return fail(error, "BasisLZ P-frame textures are not valid for a standalone static image");
    if (desc.rgb_length == 0u || desc.rgb_offset > level_size || desc.rgb_length > level_size - desc.rgb_offset)
        return fail(error, "BasisLZ RGB slice exceeds base mip level");
    if (desc.alpha_length != 0u &&
        (desc.alpha_offset > level_size || desc.alpha_length > level_size - desc.alpha_offset))
        return fail(error, "BasisLZ alpha slice exceeds base mip level");
    if (desc.alpha_length == 0u && desc.alpha_offset != 0u)
        return fail(error, "BasisLZ absent alpha slice has a nonzero offset");

    const std::uint8_t *endpoint_data = global_data + payload_offset;
    const std::uint8_t *selector_data = endpoint_data + endpoint_length;
    const std::uint8_t *tables_data = selector_data + selector_length;

    std::vector<Endpoint> endpoints;
    std::vector<Selector> selectors;
    SliceTables tables;
    if (!decodeEndpoints(endpoint_data, endpoint_length, endpoint_count, &endpoints, error) ||
        !decodeSelectors(selector_data, selector_length, selector_count, &selectors, error) ||
        !decodeSliceTables(tables_data, tables_length, &tables, error))
        return false;

    const std::size_t blocks_x = (static_cast<std::size_t>(width) + 3u) / 4u;
    const std::size_t blocks_y = (static_cast<std::size_t>(height) + 3u) / 4u;
    std::vector<BlockRef> rgb_blocks;
    if (!decodeSlice(
            level_data + desc.rgb_offset,
            desc.rgb_length,
            blocks_x,
            blocks_y,
            endpoint_count,
            selector_count,
            tables,
            &rgb_blocks,
            error))
        return false;

    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
        pixel_count > std::numeric_limits<std::size_t>::max() / 4u)
        return fail(error, "ETC1S image dimensions overflow");
    image->width = width;
    image->height = height;
    image->rgba.assign(pixel_count * 4u, 255u);
    image->meaningful_alpha = false;
    renderSlice(rgb_blocks, endpoints, selectors, blocks_x, width, height, &image->rgba, false);

    if (desc.alpha_length != 0u) {
        std::vector<BlockRef> alpha_blocks;
        if (!decodeSlice(
                level_data + desc.alpha_offset,
                desc.alpha_length,
                blocks_x,
                blocks_y,
                endpoint_count,
                selector_count,
                tables,
                &alpha_blocks,
                error))
            return false;
        renderSlice(alpha_blocks, endpoints, selectors, blocks_x, width, height, &image->rgba, true);
        for (std::size_t offset = 3u; offset < image->rgba.size(); offset += 4u) {
            if (image->rgba[offset] != 255u) {
                image->meaningful_alpha = true;
                break;
            }
        }
    }
    return true;
}

} // namespace Models::Images::Etc1s
