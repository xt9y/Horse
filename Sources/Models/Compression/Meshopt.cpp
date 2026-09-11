#include "Models/Compression/Meshopt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Models::Compression {
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

void put16le(std::uint8_t *data, std::uint16_t value)
{
    data[0] = static_cast<std::uint8_t>(value & 0xffu);
    data[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void put32le(std::uint8_t *data, std::uint32_t value)
{
    data[0] = static_cast<std::uint8_t>(value & 0xffu);
    data[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    data[2] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    data[3] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::int16_t i16le(const std::uint8_t *data)
{
    return static_cast<std::int16_t>(u16le(data));
}

void putI16le(std::uint8_t *data, std::int16_t value)
{
    put16le(data, static_cast<std::uint16_t>(value));
}

int zigzag8(std::uint8_t value)
{
    return (value & 1u) != 0u
        ? -static_cast<int>((value >> 1u) + 1u)
        : static_cast<int>(value >> 1u);
}

std::int32_t zigzag32(std::uint32_t value)
{
    return (value & 1u) != 0u
        ? -static_cast<std::int32_t>((value >> 1u) + 1u)
        : static_cast<std::int32_t>(value >> 1u);
}

bool varint(
    const std::uint8_t *&cursor,
    const std::uint8_t *end,
    std::uint32_t *out,
    std::string *error)
{
    if (!out) return fail(error, "Meshopt varint output is null");
    std::uint32_t value = 0u;
    unsigned int shift = 0u;
    for (unsigned int byte = 0u; byte < 5u; ++byte) {
        if (cursor == end) return fail(error, "truncated Meshopt varint");
        const std::uint8_t input = *cursor++;
        if (shift == 28u && (input & 0xf0u) != 0u)
            return fail(error, "Meshopt varint overflows 32 bits");
        value |= static_cast<std::uint32_t>(input & 0x7fu) << shift;
        if ((input & 0x80u) == 0u) {
            *out = value;
            return true;
        }
        shift += 7u;
    }
    return fail(error, "Meshopt varint is too long");
}

bool decodeGroup(
    const std::uint8_t *&cursor,
    const std::uint8_t *end,
    std::uint8_t mode,
    std::array<std::uint8_t, 16> *values,
    std::string *error)
{
    if (!values) return fail(error, "Meshopt group output is null");
    values->fill(0u);
    if (mode == 0u) return true;

    if (mode == 3u) {
        if (static_cast<std::size_t>(end - cursor) < values->size())
            return fail(error, "truncated Meshopt raw delta group");
        std::copy_n(cursor, values->size(), values->begin());
        cursor += values->size();
        return true;
    }

    const unsigned int bits = mode == 1u ? 2u : 4u;
    const std::size_t packed_size = mode == 1u ? 4u : 8u;
    const std::uint8_t sentinel = mode == 1u ? 3u : 15u;
    if (static_cast<std::size_t>(end - cursor) < packed_size)
        return fail(error, "truncated Meshopt packed delta group");

    const std::uint8_t *packed = cursor;
    cursor += packed_size;
    for (std::size_t index = 0u; index < values->size(); ++index) {
        std::uint8_t code = 0u;
        if (bits == 2u) {
            const unsigned int shift = 6u - static_cast<unsigned int>((index & 3u) * 2u);
            code = static_cast<std::uint8_t>((packed[index >> 2u] >> shift) & 3u);
        } else {
            const unsigned int shift = (index & 1u) == 0u ? 4u : 0u;
            code = static_cast<std::uint8_t>((packed[index >> 1u] >> shift) & 15u);
        }

        if (code == sentinel) {
            if (cursor == end) return fail(error, "truncated Meshopt sentinel delta");
            code = *cursor++;
        }
        (*values)[index] = code;
    }
    return true;
}

bool decodeAttributes(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::size_t stride,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!data || !output) return fail(error, "Meshopt attribute input/output is null");
    if (stride == 0u || stride > 256u || (stride % 4u) != 0u)
        return fail(error, "invalid Meshopt attribute stride");
    if (count > std::numeric_limits<std::size_t>::max() / stride)
        return fail(error, "Meshopt attribute output size overflows");

    const std::size_t tail_size = std::max<std::size_t>(stride, 32u);
    if (size < 1u + tail_size || data[0] != 0xa0u)
        return fail(error, "invalid Meshopt attribute stream header");

    const std::uint8_t *cursor = data + 1u;
    const std::uint8_t *tail = data + size - tail_size;
    std::vector<std::uint8_t> previous(stride);
    std::copy_n(tail, stride, previous.begin());
    output->assign(count * stride, 0u);

    const std::size_t maximum_block = std::min<std::size_t>((8192u / stride) & ~std::size_t{15u}, 256u);
    if (maximum_block == 0u) return fail(error, "Meshopt attribute stride is too large");

    std::size_t first = 0u;
    while (first < count) {
        const std::size_t block_count = std::min(count - first, maximum_block);
        const std::size_t group_count = (block_count + 15u) / 16u;
        const std::size_t header_bytes = (group_count + 3u) / 4u;

        for (std::size_t byte = 0u; byte < stride; ++byte) {
            if (static_cast<std::size_t>(tail - cursor) < header_bytes)
                return fail(error, "truncated Meshopt attribute block headers");
            const std::uint8_t *headers = cursor;
            cursor += header_bytes;
            std::uint8_t prior = previous[byte];

            for (std::size_t group = 0u; group < group_count; ++group) {
                const std::uint8_t mode = static_cast<std::uint8_t>(
                    (headers[group >> 2u] >> static_cast<unsigned int>((group & 3u) * 2u)) & 3u
                );
                std::array<std::uint8_t, 16> deltas{};
                if (!decodeGroup(cursor, tail, mode, &deltas, error)) return false;

                const std::size_t group_first = group * 16u;
                const std::size_t valid = std::min<std::size_t>(16u, block_count - group_first);
                for (std::size_t index = 0u; index < valid; ++index) {
                    prior = static_cast<std::uint8_t>(static_cast<int>(prior) + zigzag8(deltas[index]));
                    (*output)[(first + group_first + index) * stride + byte] = prior;
                }
                for (std::size_t index = valid; index < 16u; ++index)
                    prior = static_cast<std::uint8_t>(static_cast<int>(prior) + zigzag8(deltas[index]));
            }
            previous[byte] = prior;
        }
        first += block_count;
    }

    if (cursor != tail) return fail(error, "Meshopt attribute stream has trailing block data");
    return true;
}

bool storeIndex(
    std::vector<std::uint8_t> *output,
    std::size_t index,
    std::size_t stride,
    std::uint32_t value,
    std::string *error)
{
    if (!output || index > std::numeric_limits<std::size_t>::max() / stride)
        return fail(error, "Meshopt index output range overflows");
    std::uint8_t *destination = output->data() + index * stride;
    if (stride == 2u) {
        if (value > 65535u) return fail(error, "Meshopt 16-bit index exceeds 65535");
        put16le(destination, static_cast<std::uint16_t>(value));
    } else {
        put32le(destination, value);
    }
    return true;
}

bool decodeIndices(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::size_t stride,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!data || !output) return fail(error, "Meshopt index input/output is null");
    if (stride != 2u && stride != 4u) return fail(error, "Meshopt indices require 2- or 4-byte stride");
    if (count > std::numeric_limits<std::size_t>::max() / stride)
        return fail(error, "Meshopt index output size overflows");
    if (size < 5u || data[0] != 0xd1u) return fail(error, "invalid Meshopt index stream header");

    const std::uint8_t *cursor = data + 1u;
    const std::uint8_t *end = data + size - 4u;
    if (data[size - 4u] != 0u || data[size - 3u] != 0u || data[size - 2u] != 0u || data[size - 1u] != 0u)
        return fail(error, "invalid Meshopt index stream tail");

    output->assign(count * stride, 0u);
    std::array<std::uint32_t, 2> last {0u, 0u};
    for (std::size_t index = 0u; index < count; ++index) {
        std::uint32_t encoded = 0u;
        if (!varint(cursor, end, &encoded, error)) return false;
        const std::size_t baseline = encoded & 1u;
        const std::int32_t delta = (encoded & 2u) != 0u
            ? -static_cast<std::int32_t>((encoded >> 2u) + 1u)
            : static_cast<std::int32_t>(encoded >> 2u);
        last[baseline] += static_cast<std::uint32_t>(delta);
        if (!storeIndex(output, index, stride, last[baseline], error)) return false;
    }
    if (cursor != end) return fail(error, "Meshopt index stream has trailing data");
    return true;
}

struct Edge {
    std::uint32_t a = 0u;
    std::uint32_t b = 0u;
};

void pushEdge(std::array<Edge, 16> *fifo, std::size_t *used, Edge edge)
{
    const std::size_t active = std::min<std::size_t>(*used, 15u);
    for (std::size_t index = active; index > 0u; --index) (*fifo)[index] = (*fifo)[index - 1u];
    (*fifo)[0] = edge;
    *used = std::min<std::size_t>(*used + 1u, 16u);
}

void pushVertex(std::array<std::uint32_t, 16> *fifo, std::size_t *used, std::uint32_t vertex)
{
    const std::size_t active = std::min<std::size_t>(*used, 15u);
    for (std::size_t index = active; index > 0u; --index) (*fifo)[index] = (*fifo)[index - 1u];
    (*fifo)[0] = vertex;
    *used = std::min<std::size_t>(*used + 1u, 16u);
}

bool explicitTriangleIndex(
    const std::uint8_t *&cursor,
    const std::uint8_t *end,
    std::uint32_t *last,
    std::uint32_t *out,
    std::string *error)
{
    std::uint32_t encoded = 0u;
    if (!varint(cursor, end, &encoded, error)) return false;
    *last += static_cast<std::uint32_t>(zigzag32(encoded));
    *out = *last;
    return true;
}

bool recentVertex(
    const std::array<std::uint32_t, 16>& fifo,
    std::size_t used,
    std::size_t index,
    std::uint32_t *out,
    std::string *error)
{
    if (!out || index >= used) return fail(error, "Meshopt triangle stream references uninitialized vertex FIFO entry");
    *out = fifo[index];
    return true;
}

bool recentEdge(
    const std::array<Edge, 16>& fifo,
    std::size_t used,
    std::size_t index,
    Edge *out,
    std::string *error)
{
    if (!out || index >= used) return fail(error, "Meshopt triangle stream references uninitialized edge FIFO entry");
    *out = fifo[index];
    return true;
}

bool decodeTriangles(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::size_t stride,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (!data || !output) return fail(error, "Meshopt triangle input/output is null");
    if ((count % 3u) != 0u) return fail(error, "Meshopt triangle index count is not divisible by three");
    if (stride != 2u && stride != 4u) return fail(error, "Meshopt triangles require 2- or 4-byte index stride");
    if (count > std::numeric_limits<std::size_t>::max() / stride)
        return fail(error, "Meshopt triangle output size overflows");

    const std::size_t triangle_count = count / 3u;
    if (size < 1u + triangle_count + 16u || data[0] != 0xe1u)
        return fail(error, "invalid Meshopt triangle stream header");

    const std::uint8_t *codes = data + 1u;
    const std::uint8_t *cursor = codes + triangle_count;
    const std::uint8_t *codeaux = data + size - 16u;
    if (cursor > codeaux) return fail(error, "truncated Meshopt triangle stream");
    if (codeaux[14] != 0u || codeaux[15] != 0u)
        return fail(error, "invalid Meshopt triangle lookup tail");
    for (std::size_t index = 0u; index < 14u; ++index)
        if ((codeaux[index] & 0x0fu) == 0x0fu || (codeaux[index] >> 4u) == 0x0fu)
            return fail(error, "invalid Meshopt triangle lookup table");

    output->assign(count * stride, 0u);
    std::array<Edge, 16> edge_fifo{};
    std::array<std::uint32_t, 16> vertex_fifo{};
    std::size_t edge_used = 0u;
    std::size_t vertex_used = 0u;
    std::uint32_t next = 0u;
    std::uint32_t last = 0u;

    for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
        const std::uint8_t code = codes[triangle];
        const std::uint8_t x = code >> 4u;
        const std::uint8_t y = code & 0x0fu;
        std::uint32_t a = 0u, b = 0u, c = 0u;

        if (x < 0x0fu) {
            Edge edge{};
            if (!recentEdge(edge_fifo, edge_used, x, &edge, error)) return false;
            a = edge.a;
            b = edge.b;
            if (y == 0u) {
                c = next++;
                pushEdge(&edge_fifo, &edge_used, {c, b});
                pushEdge(&edge_fifo, &edge_used, {a, c});
                pushVertex(&vertex_fifo, &vertex_used, c);
            } else if (y < 0x0du) {
                if (!recentVertex(vertex_fifo, vertex_used, y, &c, error)) return false;
                pushEdge(&edge_fifo, &edge_used, {c, b});
                pushEdge(&edge_fifo, &edge_used, {a, c});
            } else if (y == 0x0du || y == 0x0eu) {
                c = y == 0x0du ? last - 1u : last + 1u;
                last = c;
                pushEdge(&edge_fifo, &edge_used, {c, b});
                pushEdge(&edge_fifo, &edge_used, {a, c});
                pushVertex(&vertex_fifo, &vertex_used, c);
            } else {
                if (!explicitTriangleIndex(cursor, codeaux, &last, &c, error)) return false;
                pushEdge(&edge_fifo, &edge_used, {c, b});
                pushEdge(&edge_fifo, &edge_used, {a, c});
                pushVertex(&vertex_fifo, &vertex_used, c);
            }
        } else if (y < 0x0eu) {
            const std::uint8_t aux = codeaux[y];
            const std::uint8_t z = aux >> 4u;
            const std::uint8_t w = aux & 0x0fu;
            a = next++;
            if (z == 0u) b = next++;
            else if (!recentVertex(vertex_fifo, vertex_used, static_cast<std::size_t>(z - 1u), &b, error)) return false;
            if (w == 0u) c = next++;
            else if (!recentVertex(vertex_fifo, vertex_used, static_cast<std::size_t>(w - 1u), &c, error)) return false;
            pushEdge(&edge_fifo, &edge_used, {b, a});
            pushEdge(&edge_fifo, &edge_used, {c, b});
            pushEdge(&edge_fifo, &edge_used, {a, c});
            pushVertex(&vertex_fifo, &vertex_used, a);
            if (z == 0u) pushVertex(&vertex_fifo, &vertex_used, b);
            if (w == 0u) pushVertex(&vertex_fifo, &vertex_used, c);
        } else {
            if (cursor == codeaux) return fail(error, "truncated Meshopt triangle auxiliary byte");
            const std::uint8_t aux = *cursor++;
            const std::uint8_t z = aux >> 4u;
            const std::uint8_t w = aux & 0x0fu;
            if (aux == 0u) next = 0u;

            if (code == 0xfeu) a = next++;
            else if (!explicitTriangleIndex(cursor, codeaux, &last, &a, error)) return false;

            if (z == 0u) b = next++;
            else if (z < 0x0fu) {
                if (!recentVertex(vertex_fifo, vertex_used, static_cast<std::size_t>(z - 1u), &b, error)) return false;
            } else if (!explicitTriangleIndex(cursor, codeaux, &last, &b, error)) return false;

            if (w == 0u) c = next++;
            else if (w < 0x0fu) {
                if (!recentVertex(vertex_fifo, vertex_used, static_cast<std::size_t>(w - 1u), &c, error)) return false;
            } else if (!explicitTriangleIndex(cursor, codeaux, &last, &c, error)) return false;

            pushEdge(&edge_fifo, &edge_used, {b, a});
            pushEdge(&edge_fifo, &edge_used, {c, b});
            pushEdge(&edge_fifo, &edge_used, {a, c});
            pushVertex(&vertex_fifo, &vertex_used, a);
            if (z == 0u || z == 0x0fu) pushVertex(&vertex_fifo, &vertex_used, b);
            if (w == 0u || w == 0x0fu) pushVertex(&vertex_fifo, &vertex_used, c);
        }

        const std::size_t base = triangle * 3u;
        if (!storeIndex(output, base + 0u, stride, a, error) ||
            !storeIndex(output, base + 1u, stride, b, error) ||
            !storeIndex(output, base + 2u, stride, c, error))
            return false;
    }

    if (cursor != codeaux) return fail(error, "Meshopt triangle stream has trailing data");
    return true;
}

std::int32_t clampSigned(long value, std::int32_t minimum, std::int32_t maximum)
{
    return static_cast<std::int32_t>(std::clamp<long>(value, minimum, maximum));
}

bool filterOctahedral(std::vector<std::uint8_t> *data, std::size_t count, std::size_t stride, std::string *error)
{
    if (!data || (stride != 4u && stride != 8u))
        return fail(error, "Meshopt OCTAHEDRAL filter requires stride 4 or 8");

    for (std::size_t element = 0u; element < count; ++element) {
        std::uint8_t *value = data->data() + element * stride;
        const bool wide = stride == 8u;
        const std::int32_t ix = wide ? i16le(value + 0u) : static_cast<std::int8_t>(value[0]);
        const std::int32_t iy = wide ? i16le(value + 2u) : static_cast<std::int8_t>(value[1]);
        const std::int32_t iz = wide ? i16le(value + 4u) : static_cast<std::int8_t>(value[2]);
        const std::int32_t passthrough = wide ? i16le(value + 6u) : static_cast<std::int8_t>(value[3]);
        if (iz <= 0) return fail(error, "invalid Meshopt octahedral normalization marker");

        const float one = static_cast<float>(iz);
        float x = static_cast<float>(ix) / one;
        float y = static_cast<float>(iy) / one;
        float z = 1.0f - std::abs(x) - std::abs(y);
        const float t = std::min(z, 0.0f);
        x -= std::copysign(t, x);
        y -= std::copysign(t, y);
        const float length = std::sqrt(x * x + y * y + z * z);
        if (!(length > 0.0f)) return fail(error, "invalid Meshopt octahedral vector");
        x /= length; y /= length; z /= length;

        const std::int32_t maximum = wide ? 32767 : 127;
        const std::int32_t ox = clampSigned(std::lround(x * static_cast<float>(maximum)), -maximum, maximum);
        const std::int32_t oy = clampSigned(std::lround(y * static_cast<float>(maximum)), -maximum, maximum);
        const std::int32_t oz = clampSigned(std::lround(z * static_cast<float>(maximum)), -maximum, maximum);
        if (wide) {
            putI16le(value + 0u, static_cast<std::int16_t>(ox));
            putI16le(value + 2u, static_cast<std::int16_t>(oy));
            putI16le(value + 4u, static_cast<std::int16_t>(oz));
            putI16le(value + 6u, static_cast<std::int16_t>(passthrough));
        } else {
            value[0] = static_cast<std::uint8_t>(static_cast<std::int8_t>(ox));
            value[1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(oy));
            value[2] = static_cast<std::uint8_t>(static_cast<std::int8_t>(oz));
            value[3] = static_cast<std::uint8_t>(static_cast<std::int8_t>(passthrough));
        }
    }
    return true;
}

bool filterQuaternion(std::vector<std::uint8_t> *data, std::size_t count, std::size_t stride, std::string *error)
{
    if (!data || stride != 8u) return fail(error, "Meshopt QUATERNION filter requires stride 8");
    constexpr float range = 0.70710678118654752440f;
    for (std::size_t element = 0u; element < count; ++element) {
        std::uint8_t *value = data->data() + element * stride;
        const std::int32_t input0 = i16le(value + 0u);
        const std::int32_t input1 = i16le(value + 2u);
        const std::int32_t input2 = i16le(value + 4u);
        const std::int32_t input3 = i16le(value + 6u);
        const std::int32_t one = input3 | 3;
        if (one <= 0) return fail(error, "invalid Meshopt quaternion normalization marker");
        const float x = static_cast<float>(input0) / static_cast<float>(one) * range;
        const float y = static_cast<float>(input1) / static_cast<float>(one) * range;
        const float z = static_cast<float>(input2) / static_cast<float>(one) * range;
        const float w = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y - z * z));
        const std::size_t maximum_component = static_cast<std::size_t>(input3 & 3);
        std::array<std::int16_t, 4> result{};
        result[(maximum_component + 1u) & 3u] = static_cast<std::int16_t>(clampSigned(std::lround(x * 32767.0f), -32767, 32767));
        result[(maximum_component + 2u) & 3u] = static_cast<std::int16_t>(clampSigned(std::lround(y * 32767.0f), -32767, 32767));
        result[(maximum_component + 3u) & 3u] = static_cast<std::int16_t>(clampSigned(std::lround(z * 32767.0f), -32767, 32767));
        result[(maximum_component + 0u) & 3u] = static_cast<std::int16_t>(clampSigned(std::lround(w * 32767.0f), -32767, 32767));
        for (std::size_t component = 0u; component < 4u; ++component)
            putI16le(value + component * 2u, result[component]);
    }
    return true;
}

bool filterExponential(std::vector<std::uint8_t> *data, std::size_t count, std::size_t stride, std::string *error)
{
    if (!data || stride == 0u || (stride % 4u) != 0u)
        return fail(error, "Meshopt EXPONENTIAL filter requires stride divisible by four");
    const std::size_t words = stride / 4u;
    for (std::size_t element = 0u; element < count; ++element) {
        std::uint8_t *value = data->data() + element * stride;
        for (std::size_t word = 0u; word < words; ++word) {
            const std::uint32_t bits = u32le(value + word * 4u);
            const std::int32_t exponent = static_cast<std::int8_t>(bits >> 24u);
            std::int32_t mantissa = static_cast<std::int32_t>(bits & 0x00ffffffu);
            if ((mantissa & 0x00800000) != 0) mantissa |= static_cast<std::int32_t>(0xff000000u);
            if (exponent < -100 || exponent > 100) return fail(error, "Meshopt exponential exponent is out of range");
            const float decoded = std::ldexp(static_cast<float>(mantissa), exponent);
            std::uint32_t output_bits = 0u;
            static_assert(sizeof(output_bits) == sizeof(decoded));
            std::memcpy(&output_bits, &decoded, sizeof(decoded));
            put32le(value + word * 4u, output_bits);
        }
    }
    return true;
}

bool applyFilter(
    std::vector<std::uint8_t> *data,
    std::size_t count,
    std::size_t stride,
    MeshoptFilter filter,
    std::string *error)
{
    switch (filter) {
        case MeshoptFilter::None: return true;
        case MeshoptFilter::Octahedral: return filterOctahedral(data, count, stride, error);
        case MeshoptFilter::Quaternion: return filterQuaternion(data, count, stride, error);
        case MeshoptFilter::Exponential: return filterExponential(data, count, stride, error);
    }
    return fail(error, "unknown Meshopt filter");
}

} // namespace

bool decodeMeshopt(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::size_t stride,
    MeshoptMode mode,
    MeshoptFilter filter,
    std::vector<std::uint8_t> *output,
    std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error, "Meshopt output is null");
    if (count == 0u) return fail(error, "Meshopt element count must be nonzero");

    bool decoded = false;
    switch (mode) {
        case MeshoptMode::Attributes:
            decoded = decodeAttributes(data, size, count, stride, output, error);
            break;
        case MeshoptMode::Triangles:
            if (filter != MeshoptFilter::None) return fail(error, "Meshopt triangle mode cannot use a filter");
            decoded = decodeTriangles(data, size, count, stride, output, error);
            break;
        case MeshoptMode::Indices:
            if (filter != MeshoptFilter::None) return fail(error, "Meshopt index mode cannot use a filter");
            decoded = decodeIndices(data, size, count, stride, output, error);
            break;
    }
    if (!decoded) return false;
    return mode != MeshoptMode::Attributes || applyFilter(output, count, stride, filter, error);
}

} // namespace Models::Compression
