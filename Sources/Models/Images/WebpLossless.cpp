#include "Models/Images/WebpLossless.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Models::Images::WebpLossless {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

class Bits {
public:
    Bits(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    bool read(unsigned int count, std::uint32_t *out, std::string *error)
    {
        if (!out || count > 32u) return fail(error, "invalid WebP lossless bit read");
        if (bit_ > size_ * 8u || count > size_ * 8u - bit_)
            return fail(error, "truncated WebP lossless bitstream");
        std::uint32_t value = 0u;
        for (unsigned int index = 0u; index < count; ++index) {
            const std::size_t position = bit_ + index;
            value |= static_cast<std::uint32_t>(
                (data_[position >> 3u] >> static_cast<unsigned int>(position & 7u)) & 1u
            ) << index;
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

std::uint8_t channel(std::uint32_t pixel, unsigned int shift)
{
    return static_cast<std::uint8_t>((pixel >> shift) & 0xffu);
}

std::uint32_t argb(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return (static_cast<std::uint32_t>(a) << 24u) |
        (static_cast<std::uint32_t>(r) << 16u) |
        (static_cast<std::uint32_t>(g) << 8u) |
        static_cast<std::uint32_t>(b);
}

std::uint32_t addPixels(std::uint32_t a, std::uint32_t b)
{
    return argb(
        static_cast<std::uint8_t>(channel(a, 24u) + channel(b, 24u)),
        static_cast<std::uint8_t>(channel(a, 16u) + channel(b, 16u)),
        static_cast<std::uint8_t>(channel(a, 8u) + channel(b, 8u)),
        static_cast<std::uint8_t>(channel(a, 0u) + channel(b, 0u))
    );
}

std::size_t divided(std::size_t value, std::size_t divisor)
{
    return value / divisor + (value % divisor != 0u ? 1u : 0u);
}

struct HuffmanNode {
    std::array<int, 2> child {{-1, -1}};
    int symbol = -1;
};

struct Huffman {
    std::vector<HuffmanNode> nodes;
    int single = -1;

    bool valid() const { return single >= 0 || !nodes.empty(); }
};

std::uint32_t reverseBits(std::uint32_t value, unsigned int count)
{
    std::uint32_t result = 0u;
    for (unsigned int bit = 0u; bit < count; ++bit) {
        result = (result << 1u) | (value & 1u);
        value >>= 1u;
    }
    return result;
}

bool buildHuffman(const std::vector<std::uint8_t>& lengths, Huffman *table, std::string *error)
{
    if (!table || lengths.empty()) return fail(error, "invalid WebP Huffman table");
    table->nodes.clear();
    table->single = -1;

    unsigned int maximum = 0u;
    std::size_t nonzero = 0u;
    int only = -1;
    for (std::size_t symbol = 0u; symbol < lengths.size(); ++symbol) {
        if (lengths[symbol] == 0u) continue;
        if (lengths[symbol] > 15u) return fail(error, "WebP Huffman code length exceeds 15 bits");
        maximum = std::max<unsigned int>(maximum, lengths[symbol]);
        ++nonzero;
        only = static_cast<int>(symbol);
    }
    if (nonzero == 0u) return fail(error, "WebP Huffman table has no symbols");
    if (nonzero == 1u) {
        table->single = only;
        return true;
    }

    std::array<std::uint32_t, 16> counts{};
    for (const std::uint8_t length : lengths)
        if (length != 0u) ++counts[length];

    std::uint64_t kraft = 0u;
    for (unsigned int length = 1u; length <= maximum; ++length)
        kraft += static_cast<std::uint64_t>(counts[length]) << (maximum - length);
    if (kraft != (std::uint64_t{1u} << maximum))
        return fail(error, "WebP Huffman code lengths do not form a complete tree");

    std::array<std::uint32_t, 16> next{};
    std::uint32_t code = 0u;
    for (unsigned int length = 1u; length <= maximum; ++length) {
        code = (code + counts[length - 1u]) << 1u;
        next[length] = code;
    }

    table->nodes.push_back({});
    for (std::size_t symbol = 0u; symbol < lengths.size(); ++symbol) {
        const unsigned int length = lengths[symbol];
        if (length == 0u) continue;
        const std::uint32_t canonical = next[length]++;
        const std::uint32_t stream_code = reverseBits(canonical, length);
        int node = 0;
        for (unsigned int depth = 0u; depth < length; ++depth) {
            if (table->nodes[static_cast<std::size_t>(node)].symbol >= 0)
                return fail(error, "WebP Huffman prefix collision");
            const unsigned int branch = (stream_code >> depth) & 1u;
            int& child = table->nodes[static_cast<std::size_t>(node)].child[branch];
            if (child < 0) {
                child = static_cast<int>(table->nodes.size());
                table->nodes.push_back({});
            }
            node = child;
        }
        HuffmanNode& leaf = table->nodes[static_cast<std::size_t>(node)];
        if (leaf.symbol >= 0 || leaf.child[0] >= 0 || leaf.child[1] >= 0)
            return fail(error, "WebP Huffman duplicate/prefix symbol");
        leaf.symbol = static_cast<int>(symbol);
    }
    return true;
}

bool readSymbol(Bits *bits, const Huffman& table, std::uint32_t *symbol, std::string *error)
{
    if (!bits || !symbol || !table.valid()) return fail(error, "invalid WebP Huffman decode state");
    if (table.single >= 0) {
        *symbol = static_cast<std::uint32_t>(table.single);
        return true;
    }
    int node = 0;
    for (unsigned int depth = 0u; depth < 15u; ++depth) {
        if (node < 0 || static_cast<std::size_t>(node) >= table.nodes.size())
            return fail(error, "WebP Huffman bitstream references invalid node");
        if (table.nodes[static_cast<std::size_t>(node)].symbol >= 0) {
            *symbol = static_cast<std::uint32_t>(table.nodes[static_cast<std::size_t>(node)].symbol);
            return true;
        }
        std::uint32_t branch = 0u;
        if (!bits->read(1u, &branch, error)) return false;
        node = table.nodes[static_cast<std::size_t>(node)].child[branch & 1u];
    }
    if (node >= 0 && static_cast<std::size_t>(node) < table.nodes.size() &&
        table.nodes[static_cast<std::size_t>(node)].symbol >= 0) {
        *symbol = static_cast<std::uint32_t>(table.nodes[static_cast<std::size_t>(node)].symbol);
        return true;
    }
    return fail(error, "WebP Huffman code exceeds 15 bits");
}

bool readPrefixCode(Bits *bits, std::size_t alphabet, Huffman *table, std::string *error)
{
    if (!bits || !table || alphabet == 0u) return fail(error, "invalid WebP prefix code request");
    std::uint32_t simple = 0u;
    if (!bits->read(1u, &simple, error)) return false;
    std::vector<std::uint8_t> lengths(alphabet, 0u);

    if (simple != 0u) {
        std::uint32_t count_minus_one = 0u;
        std::uint32_t first_eight = 0u;
        if (!bits->read(1u, &count_minus_one, error) || !bits->read(1u, &first_eight, error)) return false;
        const std::size_t count = static_cast<std::size_t>(count_minus_one + 1u);
        std::uint32_t first = 0u;
        if (!bits->read(first_eight != 0u ? 8u : 1u, &first, error)) return false;
        if (first >= alphabet) return fail(error, "WebP simple prefix symbol exceeds alphabet");
        lengths[first] = 1u;
        if (count == 2u) {
            std::uint32_t second = 0u;
            if (!bits->read(8u, &second, error)) return false;
            if (second >= alphabet) return fail(error, "WebP simple prefix symbol exceeds alphabet");
            lengths[second] = 1u;
        }
        return buildHuffman(lengths, table, error);
    }

    static constexpr std::array<std::uint8_t, 19> order {{
        17u,18u,0u,1u,2u,3u,4u,5u,16u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,
    }};
    std::uint32_t number = 0u;
    if (!bits->read(4u, &number, error)) return false;
    number += 4u;
    std::vector<std::uint8_t> code_lengths(19u, 0u);
    for (std::size_t index = 0u; index < number; ++index) {
        std::uint32_t value = 0u;
        if (!bits->read(3u, &value, error)) return false;
        code_lengths[order[index]] = static_cast<std::uint8_t>(value);
    }
    Huffman code_length_table;
    if (!buildHuffman(code_lengths, &code_length_table, error)) return false;

    std::uint32_t use_limited = 0u;
    if (!bits->read(1u, &use_limited, error)) return false;
    std::size_t maximum_symbol = alphabet;
    if (use_limited != 0u) {
        std::uint32_t selector = 0u;
        if (!bits->read(3u, &selector, error)) return false;
        const unsigned int length_bits = 2u + 2u * selector;
        std::uint32_t encoded = 0u;
        if (!bits->read(length_bits, &encoded, error)) return false;
        maximum_symbol = static_cast<std::size_t>(encoded) + 2u;
        if (maximum_symbol > alphabet) return fail(error, "WebP prefix max_symbol exceeds alphabet");
    }

    std::size_t cursor = 0u;
    std::uint8_t previous_nonzero = 8u;
    while (cursor < maximum_symbol) {
        std::uint32_t code_length_symbol = 0u;
        if (!readSymbol(bits, code_length_table, &code_length_symbol, error)) return false;
        if (code_length_symbol <= 15u) {
            lengths[cursor++] = static_cast<std::uint8_t>(code_length_symbol);
            if (code_length_symbol != 0u) previous_nonzero = static_cast<std::uint8_t>(code_length_symbol);
            continue;
        }

        std::size_t repeat = 0u;
        std::uint8_t value = 0u;
        if (code_length_symbol == 16u) {
            std::uint32_t extra = 0u;
            if (!bits->read(2u, &extra, error)) return false;
            repeat = static_cast<std::size_t>(extra) + 3u;
            value = previous_nonzero;
        } else if (code_length_symbol == 17u) {
            std::uint32_t extra = 0u;
            if (!bits->read(3u, &extra, error)) return false;
            repeat = static_cast<std::size_t>(extra) + 3u;
        } else if (code_length_symbol == 18u) {
            std::uint32_t extra = 0u;
            if (!bits->read(7u, &extra, error)) return false;
            repeat = static_cast<std::size_t>(extra) + 11u;
        } else {
            return fail(error, "invalid WebP prefix code-length symbol");
        }
        if (repeat > maximum_symbol - cursor) return fail(error, "WebP prefix code-length repeat exceeds max_symbol");
        std::fill_n(lengths.begin() + static_cast<std::ptrdiff_t>(cursor), repeat, value);
        cursor += repeat;
    }
    return buildHuffman(lengths, table, error);
}

bool prefixValue(Bits *bits, std::uint32_t code, std::uint32_t *value, std::string *error)
{
    if (!bits || !value || code > 39u) return fail(error, "invalid WebP LZ77 prefix code");
    if (code < 4u) {
        *value = code + 1u;
        return true;
    }
    const unsigned int extra_bits = (code - 2u) >> 1u;
    const std::uint32_t offset = (2u + (code & 1u)) << extra_bits;
    std::uint32_t extra = 0u;
    if (!bits->read(extra_bits, &extra, error)) return false;
    *value = offset + extra + 1u;
    return true;
}

struct PrefixGroup {
    std::array<Huffman, 5> table;
};

static constexpr std::array<std::array<int, 2>, 120> distance_map {{
    {{0,1}},{{1,0}},{{1,1}},{{-1,1}},{{0,2}},{{2,0}},{{1,2}},{{-1,2}},
    {{2,1}},{{-2,1}},{{2,2}},{{-2,2}},{{0,3}},{{3,0}},{{1,3}},{{-1,3}},
    {{3,1}},{{-3,1}},{{2,3}},{{-2,3}},{{3,2}},{{-3,2}},{{0,4}},{{4,0}},
    {{1,4}},{{-1,4}},{{4,1}},{{-4,1}},{{3,3}},{{-3,3}},{{2,4}},{{-2,4}},
    {{4,2}},{{-4,2}},{{0,5}},{{3,4}},{{-3,4}},{{4,3}},{{-4,3}},{{5,0}},
    {{1,5}},{{-1,5}},{{5,1}},{{-5,1}},{{2,5}},{{-2,5}},{{5,2}},{{-5,2}},
    {{4,4}},{{-4,4}},{{3,5}},{{-3,5}},{{5,3}},{{-5,3}},{{0,6}},{{6,0}},
    {{1,6}},{{-1,6}},{{6,1}},{{-6,1}},{{2,6}},{{-2,6}},{{6,2}},{{-6,2}},
    {{4,5}},{{-4,5}},{{5,4}},{{-5,4}},{{3,6}},{{-3,6}},{{6,3}},{{-6,3}},
    {{0,7}},{{7,0}},{{1,7}},{{-1,7}},{{5,5}},{{-5,5}},{{7,1}},{{-7,1}},
    {{4,6}},{{-4,6}},{{6,4}},{{-6,4}},{{2,7}},{{-2,7}},{{7,2}},{{-7,2}},
    {{3,7}},{{-3,7}},{{7,3}},{{-7,3}},{{5,6}},{{-5,6}},{{6,5}},{{-6,5}},
    {{8,0}},{{4,7}},{{-4,7}},{{7,4}},{{-7,4}},{{8,1}},{{8,2}},{{6,6}},
    {{-6,6}},{{8,3}},{{5,7}},{{-5,7}},{{7,5}},{{-7,5}},{{8,4}},{{6,7}},
    {{-6,7}},{{7,6}},{{-7,6}},{{8,5}},{{7,7}},{{-7,7}},{{8,6}},{{8,7}},
}};

std::uint32_t cacheIndex(std::uint32_t color, unsigned int bits)
{
    return static_cast<std::uint32_t>(color * 0x1e35a7bdu) >> (32u - bits);
}

bool distancePixels(std::uint32_t code, std::size_t width, std::size_t *distance, std::string *error)
{
    if (!distance || code == 0u) return fail(error, "invalid WebP distance code");
    if (code > 120u) {
        *distance = static_cast<std::size_t>(code - 120u);
        return *distance != 0u;
    }
    const auto& pair = distance_map[code - 1u];
    const long long value = static_cast<long long>(pair[0]) +
        static_cast<long long>(pair[1]) * static_cast<long long>(width);
    *distance = static_cast<std::size_t>(std::max<long long>(value, 1));
    return true;
}

bool decodeImageData(
    Bits *bits,
    std::size_t width,
    std::size_t height,
    bool allow_meta_prefix,
    std::vector<std::uint32_t> *pixels,
    std::string *error)
{
    if (!bits || !pixels || width == 0u || height == 0u ||
        width > std::numeric_limits<std::size_t>::max() / height)
        return fail(error, "invalid WebP image-data dimensions");
    const std::size_t count = width * height;

    std::uint32_t has_cache = 0u;
    if (!bits->read(1u, &has_cache, error)) return false;
    unsigned int cache_bits = 0u;
    std::size_t cache_size = 0u;
    if (has_cache != 0u) {
        std::uint32_t encoded = 0u;
        if (!bits->read(4u, &encoded, error)) return false;
        if (encoded < 1u || encoded > 11u) return fail(error, "invalid WebP color-cache size");
        cache_bits = encoded;
        cache_size = std::size_t{1u} << cache_bits;
    }

    unsigned int prefix_bits = 0u;
    std::size_t prefix_width = 0u;
    std::vector<std::uint32_t> entropy_image;
    std::size_t group_count = 1u;
    if (allow_meta_prefix) {
        std::uint32_t has_meta = 0u;
        if (!bits->read(1u, &has_meta, error)) return false;
        if (has_meta != 0u) {
            std::uint32_t encoded = 0u;
            if (!bits->read(3u, &encoded, error)) return false;
            prefix_bits = static_cast<unsigned int>(encoded + 2u);
            const std::size_t block = std::size_t{1u} << prefix_bits;
            prefix_width = divided(width, block);
            const std::size_t prefix_height = divided(height, block);
            if (!decodeImageData(bits, prefix_width, prefix_height, false, &entropy_image, error)) return false;
            std::uint32_t largest = 0u;
            for (const std::uint32_t pixel : entropy_image)
                largest = std::max(largest, (pixel >> 8u) & 0xffffu);
            group_count = static_cast<std::size_t>(largest) + 1u;
            if (group_count > 65536u) return fail(error, "WebP meta-prefix group count exceeds 65536");
        }
    }

    std::vector<PrefixGroup> groups(group_count);
    const std::size_t green_alphabet = 256u + 24u + cache_size;
    for (PrefixGroup& group : groups) {
        if (!readPrefixCode(bits, green_alphabet, &group.table[0], error) ||
            !readPrefixCode(bits, 256u, &group.table[1], error) ||
            !readPrefixCode(bits, 256u, &group.table[2], error) ||
            !readPrefixCode(bits, 256u, &group.table[3], error) ||
            !readPrefixCode(bits, 40u, &group.table[4], error))
            return false;
    }

    std::vector<std::uint32_t> cache(cache_size, 0u);
    pixels->clear();
    pixels->reserve(count);
    while (pixels->size() < count) {
        const std::size_t position = pixels->size();
        const std::size_t x = position % width;
        const std::size_t y = position / width;
        std::size_t group_index = 0u;
        if (!entropy_image.empty()) {
            const std::size_t meta_position = (y >> prefix_bits) * prefix_width + (x >> prefix_bits);
            if (meta_position >= entropy_image.size()) return fail(error, "WebP entropy-image index exceeds bounds");
            group_index = (entropy_image[meta_position] >> 8u) & 0xffffu;
            if (group_index >= groups.size()) return fail(error, "WebP meta-prefix code exceeds group count");
        }
        PrefixGroup& group = groups[group_index];
        std::uint32_t green_symbol = 0u;
        if (!readSymbol(bits, group.table[0], &green_symbol, error)) return false;

        if (green_symbol < 256u) {
            std::uint32_t red = 0u;
            std::uint32_t blue = 0u;
            std::uint32_t alpha = 0u;
            if (!readSymbol(bits, group.table[1], &red, error) ||
                !readSymbol(bits, group.table[2], &blue, error) ||
                !readSymbol(bits, group.table[3], &alpha, error))
                return false;
            const std::uint32_t color = argb(
                static_cast<std::uint8_t>(alpha),
                static_cast<std::uint8_t>(red),
                static_cast<std::uint8_t>(green_symbol),
                static_cast<std::uint8_t>(blue)
            );
            pixels->push_back(color);
            if (!cache.empty()) cache[cacheIndex(color, cache_bits)] = color;
            continue;
        }

        if (green_symbol < 280u) {
            std::uint32_t length = 0u;
            if (!prefixValue(bits, green_symbol - 256u, &length, error)) return false;
            std::uint32_t distance_prefix = 0u;
            if (!readSymbol(bits, group.table[4], &distance_prefix, error)) return false;
            std::uint32_t distance_code = 0u;
            if (!prefixValue(bits, distance_prefix, &distance_code, error)) return false;
            std::size_t distance = 0u;
            if (!distancePixels(distance_code, width, &distance, error)) return false;
            if (distance > pixels->size()) return fail(error, "WebP backward reference precedes image start");
            if (length > count - pixels->size()) return fail(error, "WebP backward reference exceeds image size");
            for (std::size_t copied = 0u; copied < length; ++copied) {
                const std::uint32_t color = (*pixels)[pixels->size() - distance];
                pixels->push_back(color);
                if (!cache.empty()) cache[cacheIndex(color, cache_bits)] = color;
            }
            continue;
        }

        if (cache.empty()) return fail(error, "WebP color-cache symbol used without a cache");
        const std::size_t cache_index = static_cast<std::size_t>(green_symbol - 280u);
        if (cache_index >= cache.size()) return fail(error, "WebP color-cache symbol exceeds cache size");
        const std::uint32_t color = cache[cache_index];
        pixels->push_back(color);
        cache[cacheIndex(color, cache_bits)] = color;
    }
    return true;
}

enum class TransformType : std::uint8_t {
    Predictor = 0,
    Color = 1,
    SubtractGreen = 2,
    ColorIndexing = 3,
};

struct Transform {
    TransformType type = TransformType::Predictor;
    unsigned int size_bits = 0u;
    unsigned int index_bits = 0u;
    std::size_t width_before = 0u;
    std::vector<std::uint32_t> data;
};

std::uint8_t average(std::uint8_t a, std::uint8_t b)
{
    return static_cast<std::uint8_t>((static_cast<unsigned int>(a) + b) >> 1u);
}

std::uint32_t averagePixel(std::uint32_t a, std::uint32_t b)
{
    return argb(
        average(channel(a, 24u), channel(b, 24u)),
        average(channel(a, 16u), channel(b, 16u)),
        average(channel(a, 8u), channel(b, 8u)),
        average(channel(a, 0u), channel(b, 0u))
    );
}

std::uint8_t clamp8(int value)
{
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint32_t selectPixel(std::uint32_t left, std::uint32_t top, std::uint32_t top_left)
{
    int left_distance = 0;
    int top_distance = 0;
    for (const unsigned int shift : {24u, 16u, 8u, 0u}) {
        const int estimate = static_cast<int>(channel(left, shift)) +
            static_cast<int>(channel(top, shift)) - static_cast<int>(channel(top_left, shift));
        left_distance += std::abs(estimate - static_cast<int>(channel(left, shift)));
        top_distance += std::abs(estimate - static_cast<int>(channel(top, shift)));
    }
    return left_distance < top_distance ? left : top;
}

std::uint32_t clampAddFull(std::uint32_t left, std::uint32_t top, std::uint32_t top_left)
{
    return argb(
        clamp8(static_cast<int>(channel(left,24u)) + channel(top,24u) - channel(top_left,24u)),
        clamp8(static_cast<int>(channel(left,16u)) + channel(top,16u) - channel(top_left,16u)),
        clamp8(static_cast<int>(channel(left,8u)) + channel(top,8u) - channel(top_left,8u)),
        clamp8(static_cast<int>(channel(left,0u)) + channel(top,0u) - channel(top_left,0u))
    );
}

std::uint32_t clampAddHalf(std::uint32_t average_value, std::uint32_t top_left)
{
    return argb(
        clamp8(static_cast<int>(channel(average_value,24u)) +
            (static_cast<int>(channel(average_value,24u)) - channel(top_left,24u)) / 2),
        clamp8(static_cast<int>(channel(average_value,16u)) +
            (static_cast<int>(channel(average_value,16u)) - channel(top_left,16u)) / 2),
        clamp8(static_cast<int>(channel(average_value,8u)) +
            (static_cast<int>(channel(average_value,8u)) - channel(top_left,8u)) / 2),
        clamp8(static_cast<int>(channel(average_value,0u)) +
            (static_cast<int>(channel(average_value,0u)) - channel(top_left,0u)) / 2)
    );
}

std::uint32_t predictor(
    unsigned int mode,
    std::uint32_t left,
    std::uint32_t top,
    std::uint32_t top_right,
    std::uint32_t top_left)
{
    switch (mode) {
        case 0u: return 0xff000000u;
        case 1u: return left;
        case 2u: return top;
        case 3u: return top_right;
        case 4u: return top_left;
        case 5u: return averagePixel(averagePixel(left, top_right), top);
        case 6u: return averagePixel(left, top_left);
        case 7u: return averagePixel(left, top);
        case 8u: return averagePixel(top_left, top);
        case 9u: return averagePixel(top, top_right);
        case 10u: return averagePixel(averagePixel(left, top_left), averagePixel(top, top_right));
        case 11u: return selectPixel(left, top, top_left);
        case 12u: return clampAddFull(left, top, top_left);
        case 13u: return clampAddHalf(averagePixel(left, top), top_left);
        default: return 0xff000000u;
    }
}

int colorDelta(std::uint8_t transform, std::uint8_t color)
{
    return (static_cast<int>(static_cast<std::int8_t>(transform)) *
        static_cast<int>(static_cast<std::int8_t>(color))) >> 5;
}

bool applyTransforms(
    const std::vector<Transform>& transforms,
    std::size_t original_width,
    std::size_t height,
    std::vector<std::uint32_t> *pixels,
    std::string *error)
{
    if (!pixels) return fail(error, "null WebP transform output");
    for (auto it = transforms.rbegin(); it != transforms.rend(); ++it) {
        const Transform& transform = *it;
        if (transform.type == TransformType::ColorIndexing) {
            if (transform.data.empty()) return fail(error, "empty WebP color-index table");
            const std::size_t packed_width = divided(transform.width_before, std::size_t{1u} << transform.index_bits);
            if (pixels->size() != packed_width * height)
                return fail(error, "WebP color-index input dimensions do not match transform");
            std::vector<std::uint32_t> expanded(transform.width_before * height, 0u);
            const unsigned int value_bits = 8u >> transform.index_bits;
            const std::uint32_t mask = (std::uint32_t{1u} << value_bits) - 1u;
            for (std::size_t y = 0u; y < height; ++y) {
                for (std::size_t x = 0u; x < transform.width_before; ++x) {
                    const std::uint32_t packed = (*pixels)[y * packed_width + (x >> transform.index_bits)];
                    const unsigned int shift = static_cast<unsigned int>((x & ((std::size_t{1u} << transform.index_bits) - 1u)) * value_bits);
                    const std::size_t index = (channel(packed, 8u) >> shift) & mask;
                    expanded[y * transform.width_before + x] = index < transform.data.size() ? transform.data[index] : 0u;
                }
            }
            *pixels = std::move(expanded);
            continue;
        }

        if (pixels->size() != transform.width_before * height)
            return fail(error, "WebP transform dimensions do not match decoded image");
        if (transform.type == TransformType::SubtractGreen) {
            for (std::uint32_t& pixel : *pixels) {
                const std::uint8_t a = channel(pixel, 24u);
                const std::uint8_t r = channel(pixel, 16u);
                const std::uint8_t g = channel(pixel, 8u);
                const std::uint8_t b = channel(pixel, 0u);
                pixel = argb(a, static_cast<std::uint8_t>(r + g), g, static_cast<std::uint8_t>(b + g));
            }
            continue;
        }

        const std::size_t block = std::size_t{1u} << transform.size_bits;
        const std::size_t transform_width = divided(transform.width_before, block);
        const std::size_t transform_height = divided(height, block);
        if (transform.data.size() != transform_width * transform_height)
            return fail(error, "WebP transform metadata dimensions are invalid");

        if (transform.type == TransformType::Color) {
            for (std::size_t y = 0u; y < height; ++y) {
                for (std::size_t x = 0u; x < transform.width_before; ++x) {
                    const std::uint32_t metadata = transform.data[(y >> transform.size_bits) * transform_width + (x >> transform.size_bits)];
                    const std::uint8_t green_to_red = channel(metadata, 0u);
                    const std::uint8_t green_to_blue = channel(metadata, 8u);
                    const std::uint8_t red_to_blue = channel(metadata, 16u);
                    std::uint32_t& pixel = (*pixels)[y * transform.width_before + x];
                    const std::uint8_t a = channel(pixel, 24u);
                    int r = channel(pixel, 16u);
                    const std::uint8_t g = channel(pixel, 8u);
                    int b = channel(pixel, 0u);
                    r = (r + colorDelta(green_to_red, g)) & 255;
                    b = (b + colorDelta(green_to_blue, g) +
                        colorDelta(red_to_blue, static_cast<std::uint8_t>(r))) & 255;
                    pixel = argb(a, static_cast<std::uint8_t>(r), g, static_cast<std::uint8_t>(b));
                }
            }
            continue;
        }

        if (transform.type == TransformType::Predictor) {
            for (std::size_t y = 0u; y < height; ++y) {
                for (std::size_t x = 0u; x < transform.width_before; ++x) {
                    const std::size_t position = y * transform.width_before + x;
                    const std::uint32_t residual = (*pixels)[position];
                    std::uint32_t prediction = 0xff000000u;
                    if (x == 0u && y == 0u) {
                        prediction = 0xff000000u;
                    } else if (y == 0u) {
                        prediction = (*pixels)[position - 1u];
                    } else if (x == 0u) {
                        prediction = (*pixels)[position - transform.width_before];
                    } else {
                        const std::uint32_t left = (*pixels)[position - 1u];
                        const std::uint32_t top = (*pixels)[position - transform.width_before];
                        const std::uint32_t top_left = (*pixels)[position - transform.width_before - 1u];
                        const std::uint32_t top_right = x + 1u < transform.width_before
                            ? (*pixels)[position - transform.width_before + 1u]
                            : (*pixels)[y * transform.width_before];
                        const std::uint32_t metadata = transform.data[(y >> transform.size_bits) * transform_width + (x >> transform.size_bits)];
                        const unsigned int mode = channel(metadata, 8u);
                        if (mode > 13u) return fail(error, "invalid WebP predictor mode");
                        prediction = predictor(mode, left, top, top_right, top_left);
                    }
                    (*pixels)[position] = addPixels(residual, prediction);
                }
            }
        }
    }
    if (pixels->size() != original_width * height)
        return fail(error, "WebP inverse transforms did not restore original dimensions");
    return true;
}

bool decodeBody(
    Bits *bits,
    std::size_t width,
    std::size_t height,
    std::vector<std::uint32_t> *pixels,
    std::string *error)
{
    if (!bits || !pixels || width == 0u || height == 0u) return fail(error, "invalid WebP lossless dimensions");
    std::size_t encoded_width = width;
    std::array<bool, 4> seen {{false, false, false, false}};
    std::vector<Transform> transforms;

    for (;;) {
        std::uint32_t present = 0u;
        if (!bits->read(1u, &present, error)) return false;
        if (present == 0u) break;
        std::uint32_t type_value = 0u;
        if (!bits->read(2u, &type_value, error)) return false;
        if (type_value > 3u || seen[type_value]) return fail(error, "duplicate or invalid WebP transform");
        seen[type_value] = true;

        Transform transform;
        transform.type = static_cast<TransformType>(type_value);
        transform.width_before = encoded_width;
        if (transform.type == TransformType::Predictor || transform.type == TransformType::Color) {
            std::uint32_t encoded_bits = 0u;
            if (!bits->read(3u, &encoded_bits, error)) return false;
            transform.size_bits = static_cast<unsigned int>(encoded_bits + 2u);
            const std::size_t block = std::size_t{1u} << transform.size_bits;
            if (!decodeImageData(
                    bits,
                    divided(encoded_width, block),
                    divided(height, block),
                    false,
                    &transform.data,
                    error))
                return false;
        } else if (transform.type == TransformType::ColorIndexing) {
            std::uint32_t table_minus_one = 0u;
            if (!bits->read(8u, &table_minus_one, error)) return false;
            const std::size_t table_size = static_cast<std::size_t>(table_minus_one) + 1u;
            if (!decodeImageData(bits, table_size, 1u, false, &transform.data, error)) return false;
            std::uint32_t previous = 0u;
            for (std::uint32_t& entry : transform.data) {
                entry = addPixels(previous, entry);
                previous = entry;
            }
            transform.index_bits = table_size <= 2u ? 3u : table_size <= 4u ? 2u : table_size <= 16u ? 1u : 0u;
            encoded_width = divided(encoded_width, std::size_t{1u} << transform.index_bits);
        }
        transforms.push_back(std::move(transform));
    }

    if (!decodeImageData(bits, encoded_width, height, true, pixels, error)) return false;
    return applyTransforms(transforms, width, height, pixels, error);
}

bool toImage(
    const std::vector<std::uint32_t>& pixels,
    std::size_t width,
    std::size_t height,
    Image *image,
    std::string *error)
{
    if (!image || width > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        width > std::numeric_limits<std::size_t>::max() / height || pixels.size() != width * height)
        return fail(error, "invalid WebP lossless output dimensions");
    image->width = static_cast<int>(width);
    image->height = static_cast<int>(height);
    image->rgba.resize(pixels.size() * 4u);
    image->meaningful_alpha = false;
    for (std::size_t index = 0u; index < pixels.size(); ++index) {
        const std::uint32_t pixel = pixels[index];
        const std::uint8_t a = channel(pixel, 24u);
        image->rgba[index * 4u + 0u] = channel(pixel, 16u);
        image->rgba[index * 4u + 1u] = channel(pixel, 8u);
        image->rgba[index * 4u + 2u] = channel(pixel, 0u);
        image->rgba[index * 4u + 3u] = a;
        if (a != 255u) image->meaningful_alpha = true;
    }
    return true;
}

} // namespace

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!data || size < 5u || data[0] != 0x2fu) return fail(error, "invalid WebP lossless VP8L payload");
    Bits bits(data + 1u, size - 1u);
    std::uint32_t width_minus_one = 0u;
    std::uint32_t height_minus_one = 0u;
    std::uint32_t alpha_hint = 0u;
    std::uint32_t version = 0u;
    if (!bits.read(14u, &width_minus_one, error) ||
        !bits.read(14u, &height_minus_one, error) ||
        !bits.read(1u, &alpha_hint, error) ||
        !bits.read(3u, &version, error))
        return false;
    (void)alpha_hint;
    if (version != 0u) return fail(error, "unsupported WebP lossless version");
    const std::size_t width = static_cast<std::size_t>(width_minus_one) + 1u;
    const std::size_t height = static_cast<std::size_t>(height_minus_one) + 1u;
    std::vector<std::uint32_t> pixels;
    if (!decodeBody(&bits, width, height, &pixels, error)) return false;
    return toImage(pixels, width, height, image, error);
}

bool decodeStream(
    const std::uint8_t *data,
    std::size_t size,
    int width,
    int height,
    std::vector<std::uint32_t> *argb_pixels,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !argb_pixels || width <= 0 || height <= 0)
        return fail(error, "invalid headerless WebP lossless stream");
    Bits bits(data, size);
    return decodeBody(
        &bits,
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        argb_pixels,
        error
    );
}

} // namespace Models::Images::WebpLossless
