#include "Models/Compression/Deflate.hpp"

#include "Models/Compression/Checksums.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Models::Compression {
namespace {

#if defined(_MSC_VER)
#define HORSE_DEFLATE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define HORSE_DEFLATE_INLINE inline __attribute__((always_inline))
#else
#define HORSE_DEFLATE_INLINE inline
#endif

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

class BitReader {
public:
    BitReader(const std::uint8_t *data, std::size_t size)
        : data_(data), size_(size)
    {
    }

    HORSE_DEFLATE_INLINE bool readBits(unsigned count, std::uint32_t *value)
    {
        if (!value || count > 24u) return false;
        if (!ensure(count)) return false;
        const std::uint64_t mask = count == 0u
            ? 0u
            : ((std::uint64_t{1u} << count) - 1u);
        *value = static_cast<std::uint32_t>(buffer_ & mask);
        buffer_ >>= count;
        buffered_bits_ -= count;
        consumed_bits_ += count;
        return true;
    }

    HORSE_DEFLATE_INLINE bool peekBits(unsigned count, std::uint32_t *value)
    {
        if (!value || count > 24u) return false;
        if (!ensure(count)) return false;
        const std::uint64_t mask = count == 0u
            ? 0u
            : ((std::uint64_t{1u} << count) - 1u);
        *value = static_cast<std::uint32_t>(buffer_ & mask);
        return true;
    }

    HORSE_DEFLATE_INLINE bool skipBits(unsigned count)
    {
        if (!ensure(count)) return false;
        buffer_ >>= count;
        buffered_bits_ -= count;
        consumed_bits_ += count;
        return true;
    }

    HORSE_DEFLATE_INLINE void consumeBuffered(unsigned count)
    {
        buffer_ >>= count;
        buffered_bits_ -= count;
        consumed_bits_ += count;
    }

    bool readBit(std::uint32_t *value)
    {
        return readBits(1u, value);
    }

    void alignByte()
    {
        const unsigned misalignment = static_cast<unsigned>(consumed_bits_ & 7u);
        if (misalignment != 0u) (void)skipBits(8u - misalignment);
    }

    bool readByte(std::uint8_t *value)
    {
        if ((consumed_bits_ & 7u) != 0u || !value) return false;
        std::uint32_t byte = 0u;
        if (!readBits(8u, &byte)) return false;
        *value = static_cast<std::uint8_t>(byte);
        return true;
    }

    bool readU16(std::uint16_t *value)
    {
        std::uint8_t lo = 0u;
        std::uint8_t hi = 0u;
        if (!readByte(&lo) || !readByte(&hi)) return false;
        *value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(lo) |
            (static_cast<std::uint16_t>(hi) << 8u)
        );
        return true;
    }

private:
    HORSE_DEFLATE_INLINE bool ensure(unsigned count)
    {
        while (buffered_bits_ < count && byte_position_ < size_) {
            buffer_ |= static_cast<std::uint64_t>(data_[byte_position_++]) << buffered_bits_;
            buffered_bits_ += 8u;
        }
        return buffered_bits_ >= count;
    }

    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t byte_position_ = 0u;
    std::size_t consumed_bits_ = 0u;
    std::uint64_t buffer_ = 0u;
    unsigned buffered_bits_ = 0u;
};

struct Huffman {
    static constexpr unsigned LookupBits = 10u;
    static constexpr std::size_t LookupSize = std::size_t{1u} << LookupBits;

    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t> symbol;
    std::array<std::uint32_t, LookupSize> lookup{};
};

std::uint32_t reverseBits(std::uint32_t value, unsigned count)
{
    std::uint32_t result = 0u;
    for (unsigned bit = 0u; bit < count; ++bit) {
        result = (result << 1u) | (value & 1u);
        value >>= 1u;
    }
    return result;
}

bool buildHuffman(
    const std::uint8_t *lengths,
    std::size_t length_count,
    Huffman *table,
    std::string *error)
{
    if (!lengths || !table) return fail(error, "invalid Huffman table input");
    *table = {};
    std::uint16_t *counts = table->count.data();

    for (std::size_t index = 0u; index < length_count; ++index) {
        const std::uint8_t length = lengths[index];
        if (length > 15u) return fail(error, "invalid DEFLATE Huffman code length");
        ++counts[length];
    }

    if (counts[0] == length_count) {
        return fail(error, "empty DEFLATE Huffman tree");
    }

    int left = 1;
    for (std::size_t bits = 1u; bits <= 15u; ++bits) {
        left <<= 1;
        left -= counts[bits];
        if (left < 0) return fail(error, "oversubscribed DEFLATE Huffman tree");
    }

    std::array<std::uint16_t, 16> offsets{};
    std::uint16_t *offset_values = offsets.data();
    offset_values[1] = 0u;
    for (std::size_t bits = 1u; bits < 15u; ++bits) {
        offset_values[bits + 1u] = static_cast<std::uint16_t>(
            offset_values[bits] + counts[bits]
        );
    }

    table->symbol.resize(length_count - counts[0]);
    std::uint16_t *symbols = table->symbol.data();
    for (std::size_t symbol = 0u; symbol < length_count; ++symbol) {
        const std::uint8_t length = lengths[symbol];
        if (length == 0u) continue;
        const std::size_t destination = offset_values[length]++;
        if (destination >= table->symbol.size()) {
            return fail(error, "invalid DEFLATE Huffman symbol ordering");
        }
        symbols[destination] = static_cast<std::uint16_t>(symbol);
    }

    std::array<std::uint32_t, 16> next_code{};
    std::uint32_t *next_codes = next_code.data();
    std::uint32_t code = 0u;
    for (unsigned bits = 1u; bits <= 15u; ++bits) {
        code = (code + counts[bits - 1u]) << 1u;
        next_codes[bits] = code;
    }
    std::uint32_t *lookup = table->lookup.data();
    for (std::size_t symbol = 0u; symbol < length_count; ++symbol) {
        const unsigned length = lengths[symbol];
        if (length == 0u) continue;
        const std::uint32_t reversed = reverseBits(next_codes[length]++, length);
        if (length > Huffman::LookupBits) continue;
        const std::uint32_t entry =
            (static_cast<std::uint32_t>(length) << 16u) |
            (static_cast<std::uint32_t>(symbol) + 1u);
        const std::uint32_t stride = std::uint32_t{1u} << length;
        for (std::uint32_t slot = reversed; slot < Huffman::LookupSize; slot += stride) {
            lookup[slot] = entry;
        }
    }
    return true;
}

HORSE_DEFLATE_INLINE bool decodeSymbol(
    BitReader *reader,
    const Huffman& table,
    std::uint16_t *symbol,
    std::string *error)
{
    if (!reader || !symbol) return fail(error, "invalid Huffman decode state");

    std::uint32_t bits = 0u;
    if (reader->peekBits(Huffman::LookupBits, &bits)) {
        const std::uint32_t entry = table.lookup.data()[bits];
        if (entry != 0u) {
            const unsigned length = entry >> 16u;
            reader->consumeBuffered(length);
            *symbol = static_cast<std::uint16_t>((entry & 0xffffu) - 1u);
            return true;
        }
    }

    std::uint32_t code = 0u;
    std::uint32_t first = 0u;
    std::size_t index = 0u;
    for (std::size_t length = 1u; length <= 15u; ++length) {
        std::uint32_t bit = 0u;
        if (!reader->readBit(&bit)) return fail(error, "truncated DEFLATE Huffman code");
        code |= bit;

        const std::uint32_t count = table.count.data()[length];
        if (code >= first && code - first < count) {
            const std::size_t slot = index + static_cast<std::size_t>(code - first);
            if (slot >= table.symbol.size()) {
                return fail(error, "invalid DEFLATE Huffman symbol index");
            }
            *symbol = table.symbol.data()[slot];
            return true;
        }

        index += count;
        first = (first + count) << 1u;
        code <<= 1u;
    }
    return fail(error, "invalid DEFLATE Huffman code");
}

class InflateOutput {
public:
    InflateOutput(std::vector<std::uint8_t> *bytes, std::size_t limit)
        : bytes_(bytes), limit_(limit)
    {
        if (bytes_) bytes_->clear();
    }

    InflateOutput(std::uint8_t *bytes, std::size_t size)
        : fixed_bytes_(bytes), fixed_capacity_(size), limit_(size)
    {
    }

    HORSE_DEFLATE_INLINE std::size_t size() const { return written_; }

    HORSE_DEFLATE_INLINE bool append(std::uint8_t value, std::string *error)
    {
        if (!ensure(1u, error)) return false;
        data()[written_++] = value;
        return true;
    }

    HORSE_DEFLATE_INLINE bool appendCopy(std::size_t distance, std::size_t length, std::string *error)
    {
        if (distance == 0u || distance > written_) {
            return fail(error, "invalid DEFLATE back-reference distance");
        }
        if (!ensure(length, error)) return false;

        std::uint8_t *bytes = data();
        const std::size_t destination = written_;
        const std::size_t seed = std::min(distance, length);
        std::memcpy(bytes + destination, bytes + destination - distance, seed);
        std::size_t copied = seed;
        while (copied < length) {
            const std::size_t chunk = std::min(copied, length - copied);
            std::memcpy(bytes + destination + copied, bytes + destination, chunk);
            copied += chunk;
        }
        written_ += length;
        return true;
    }

    void finish()
    {
        if (bytes_) bytes_->resize(written_);
    }

private:
    HORSE_DEFLATE_INLINE std::uint8_t *data()
    {
        return bytes_ ? bytes_->data() : fixed_bytes_;
    }

    HORSE_DEFLATE_INLINE bool ensure(std::size_t growth, std::string *error)
    {
        if (!bytes_ && !fixed_bytes_) return fail(error, "null DEFLATE output");
        if (growth > limit_ || written_ > limit_ - growth) {
            return fail(error, "DEFLATE output limit exceeded");
        }
        const std::size_t needed = written_ + growth;
        if (fixed_bytes_) return needed <= fixed_capacity_;
        if (needed <= bytes_->size()) return true;

        std::size_t expanded = bytes_->empty() ? std::min<std::size_t>(limit_, 64u * 1024u) : bytes_->size();
        if (expanded == 0u) expanded = needed;
        while (expanded < needed) {
            const std::size_t remaining = limit_ - expanded;
            if (remaining == 0u) break;
            expanded += std::min(expanded, remaining);
        }
        if (expanded < needed) expanded = needed;
        bytes_->resize(expanded);
        return true;
    }

    std::vector<std::uint8_t> *bytes_ = nullptr;
    std::uint8_t *fixed_bytes_ = nullptr;
    std::size_t fixed_capacity_ = 0u;
    std::size_t limit_ = 0u;
    std::size_t written_ = 0u;
};

bool decodeCompressedBlock(
    BitReader *reader,
    const Huffman& literals,
    const Huffman& distances,
    InflateOutput *output,
    std::string *error)
{
    static constexpr std::array<std::uint16_t, 29> length_base = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static constexpr std::array<std::uint8_t, 29> length_extra = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static constexpr std::array<std::uint16_t, 30> distance_base = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static constexpr std::array<std::uint8_t, 30> distance_extra = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    const std::uint16_t *length_bases = length_base.data();
    const std::uint8_t *length_extras = length_extra.data();
    const std::uint16_t *distance_bases = distance_base.data();
    const std::uint8_t *distance_extras = distance_extra.data();

    for (;;) {
        std::uint16_t literal = 0u;
        if (!decodeSymbol(reader, literals, &literal, error)) return false;
        if (literal < 256u) {
            if (!output->append(static_cast<std::uint8_t>(literal), error)) return false;
            continue;
        }
        if (literal == 256u) return true;
        if (literal < 257u || literal > 285u) {
            return fail(error, "invalid DEFLATE length symbol");
        }

        const std::size_t length_index = static_cast<std::size_t>(literal - 257u);
        std::uint32_t length_bits = 0u;
        if (!reader->readBits(length_extras[length_index], &length_bits)) {
            return fail(error, "truncated DEFLATE length");
        }
        const std::size_t length = static_cast<std::size_t>(length_bases[length_index]) + length_bits;

        std::uint16_t distance_symbol = 0u;
        if (!decodeSymbol(reader, distances, &distance_symbol, error)) return false;
        if (distance_symbol >= 30u) return fail(error, "invalid DEFLATE distance symbol");
        std::uint32_t distance_bits = 0u;
        if (!reader->readBits(distance_extras[distance_symbol], &distance_bits)) {
            return fail(error, "truncated DEFLATE distance");
        }
        const std::size_t distance =
            static_cast<std::size_t>(distance_bases[distance_symbol]) + distance_bits;
        if (!output->appendCopy(distance, length, error)) return false;
    }
}

bool fixedTables(Huffman *literals, Huffman *distances, std::string *error)
{
    std::array<std::uint8_t, 288> literal_lengths{};
    for (std::size_t i = 0u; i <= 143u; ++i) literal_lengths[i] = 8u;
    for (std::size_t i = 144u; i <= 255u; ++i) literal_lengths[i] = 9u;
    for (std::size_t i = 256u; i <= 279u; ++i) literal_lengths[i] = 7u;
    for (std::size_t i = 280u; i <= 287u; ++i) literal_lengths[i] = 8u;

    std::array<std::uint8_t, 32> distance_lengths{};
    distance_lengths.fill(5u);
    return buildHuffman(literal_lengths.data(), literal_lengths.size(), literals, error) &&
        buildHuffman(distance_lengths.data(), distance_lengths.size(), distances, error);
}

bool dynamicTables(
    BitReader *reader,
    Huffman *literals,
    Huffman *distances,
    std::string *error)
{
    static constexpr std::array<std::uint8_t, 19> code_order = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    std::uint32_t raw_hlit = 0u;
    std::uint32_t raw_hdist = 0u;
    std::uint32_t raw_hclen = 0u;
    if (!reader->readBits(5u, &raw_hlit) ||
        !reader->readBits(5u, &raw_hdist) ||
        !reader->readBits(4u, &raw_hclen))
    {
        return fail(error, "truncated DEFLATE dynamic header");
    }

    const std::size_t hlit = static_cast<std::size_t>(raw_hlit) + 257u;
    const std::size_t hdist = static_cast<std::size_t>(raw_hdist) + 1u;
    const std::size_t hclen = static_cast<std::size_t>(raw_hclen) + 4u;
    if (hlit > 286u || hdist > 32u) return fail(error, "invalid DEFLATE dynamic table sizes");

    std::array<std::uint8_t, 19> code_lengths{};
    for (std::size_t i = 0u; i < hclen; ++i) {
        std::uint32_t value = 0u;
        if (!reader->readBits(3u, &value)) {
            return fail(error, "truncated DEFLATE code-length tree");
        }
        code_lengths[code_order[i]] = static_cast<std::uint8_t>(value);
    }

    Huffman code_table;
    if (!buildHuffman(code_lengths.data(), code_lengths.size(), &code_table, error)) return false;

    const std::size_t total = hlit + hdist;
    std::vector<std::uint8_t> lengths(total, 0u);
    std::size_t index = 0u;
    while (index < total) {
        std::uint16_t symbol = 0u;
        if (!decodeSymbol(reader, code_table, &symbol, error)) return false;
        if (symbol <= 15u) {
            lengths[index++] = static_cast<std::uint8_t>(symbol);
            continue;
        }

        std::size_t repeat = 0u;
        std::uint8_t value = 0u;
        if (symbol == 16u) {
            if (index == 0u) return fail(error, "DEFLATE repeat has no previous code length");
            std::uint32_t extra = 0u;
            if (!reader->readBits(2u, &extra)) return fail(error, "truncated DEFLATE repeat");
            repeat = 3u + extra;
            value = lengths[index - 1u];
        } else if (symbol == 17u) {
            std::uint32_t extra = 0u;
            if (!reader->readBits(3u, &extra)) return fail(error, "truncated DEFLATE zero repeat");
            repeat = 3u + extra;
        } else if (symbol == 18u) {
            std::uint32_t extra = 0u;
            if (!reader->readBits(7u, &extra)) return fail(error, "truncated DEFLATE long zero repeat");
            repeat = 11u + extra;
        } else {
            return fail(error, "invalid DEFLATE code-length symbol");
        }

        if (repeat > total - index) return fail(error, "DEFLATE code-length repeat overflow");
        for (std::size_t i = 0u; i < repeat; ++i) lengths[index++] = value;
    }

    if (lengths[256u] == 0u) return fail(error, "DEFLATE literal tree has no end marker");
    if (!buildHuffman(lengths.data(), hlit, literals, error)) return false;
    if (!buildHuffman(lengths.data() + hlit, hdist, distances, error)) return false;
    return true;
}

bool inflateDeflate(
    const std::uint8_t *data,
    std::size_t size,
    InflateOutput *decoded,
    std::string *error)
{
    if (!decoded) return fail(error, "null DEFLATE output");
    BitReader reader(data, size);
    bool final_block = false;
    while (!final_block) {
        std::uint32_t final = 0u;
        std::uint32_t type = 0u;
        if (!reader.readBits(1u, &final) || !reader.readBits(2u, &type)) {
            return fail(error, "truncated DEFLATE block header");
        }
        final_block = final != 0u;

        if (type == 0u) {
            reader.alignByte();
            std::uint16_t length = 0u;
            std::uint16_t inverse_length = 0u;
            if (!reader.readU16(&length) || !reader.readU16(&inverse_length)) {
                return fail(error, "truncated DEFLATE stored block header");
            }
            if (static_cast<std::uint16_t>(length ^ 0xffffu) != inverse_length) {
                return fail(error, "invalid DEFLATE stored block length");
            }
            for (std::size_t i = 0u; i < length; ++i) {
                std::uint8_t byte = 0u;
                if (!reader.readByte(&byte)) return fail(error, "truncated DEFLATE stored block");
                if (!decoded->append(byte, error)) return false;
            }
            continue;
        }

        if (type == 3u) return fail(error, "reserved DEFLATE block type");

        Huffman literals;
        Huffman distances;
        if (type == 1u) {
            if (!fixedTables(&literals, &distances, error)) return false;
        } else {
            if (!dynamicTables(&reader, &literals, &distances, error)) return false;
        }
        if (!decodeCompressedBlock(&reader, literals, distances, decoded, error)) return false;
    }
    decoded->finish();
    return true;
}

} // namespace

bool inflateZlib(
    const std::uint8_t *data,
    std::size_t size,
    std::vector<std::uint8_t> *output,
    std::string *error,
    InflateOptions options)
{
    if (error) error->clear();
    if (!output) return fail(error, "null zlib output");
    output->clear();
    if (!data || size < 6u) return fail(error, "truncated zlib stream");

    const std::uint8_t cmf = data[0];
    const std::uint8_t flg = data[1];
    if ((cmf & 0x0fu) != 8u) return fail(error, "unsupported zlib compression method");
    if ((cmf >> 4u) > 7u) return fail(error, "invalid zlib window size");
    if (((static_cast<unsigned>(cmf) << 8u) | flg) % 31u != 0u) {
        return fail(error, "invalid zlib header checksum");
    }
    if ((flg & 0x20u) != 0u) return fail(error, "zlib preset dictionary is unsupported");

    std::vector<std::uint8_t> decoded;
    InflateOutput target(&decoded, options.max_output);
    if (!inflateDeflate(data + 2u, size - 6u, &target, error)) return false;

    if (options.verify_adler32) {
        const std::uint32_t expected =
            (static_cast<std::uint32_t>(data[size - 4u]) << 24u) |
            (static_cast<std::uint32_t>(data[size - 3u]) << 16u) |
            (static_cast<std::uint32_t>(data[size - 2u]) << 8u) |
            static_cast<std::uint32_t>(data[size - 1u]);
        const std::uint32_t actual = adler32(decoded.data(), decoded.size());
        if (actual != expected) return fail(error, "zlib Adler-32 mismatch");
    }

    *output = std::move(decoded);
    return true;
}

bool inflateZlibExact(
    const std::uint8_t *data,
    std::size_t size,
    std::uint8_t *output,
    std::size_t output_size,
    std::string *error,
    InflateOptions options)
{
    if (error) error->clear();
    if (!output && output_size != 0u) return fail(error, "null zlib output");
    if (!data || size < 6u) return fail(error, "truncated zlib stream");
    if (output_size > options.max_output) return fail(error, "DEFLATE output limit exceeded");

    const std::uint8_t cmf = data[0];
    const std::uint8_t flg = data[1];
    if ((cmf & 0x0fu) != 8u) return fail(error, "unsupported zlib compression method");
    if ((cmf >> 4u) > 7u) return fail(error, "invalid zlib window size");
    if (((static_cast<unsigned>(cmf) << 8u) | flg) % 31u != 0u)
        return fail(error, "invalid zlib header checksum");
    if ((flg & 0x20u) != 0u) return fail(error, "zlib preset dictionary is unsupported");

    InflateOutput target(output, output_size);
    if (!inflateDeflate(data + 2u, size - 6u, &target, error)) return false;
    if (target.size() != output_size) return fail(error, "zlib output size mismatch");

    if (options.verify_adler32) {
        const std::uint32_t expected =
            (static_cast<std::uint32_t>(data[size - 4u]) << 24u) |
            (static_cast<std::uint32_t>(data[size - 3u]) << 16u) |
            (static_cast<std::uint32_t>(data[size - 2u]) << 8u) |
            static_cast<std::uint32_t>(data[size - 1u]);
        const std::uint32_t actual = adler32(output, output_size);
        if (actual != expected) return fail(error, "zlib Adler-32 mismatch");
    }
    return true;
}

#undef HORSE_DEFLATE_INLINE

} // namespace Models::Compression
