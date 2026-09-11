#include "Models/Images/Webp.hpp"

#include "Models/Images/WebpAlpha.hpp"
#include "Models/Images/WebpLossless.hpp"
#include "Models/Images/WebpVp8.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Models::Images::Webp {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint32_t u32le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

std::uint32_t u24le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u);
}

bool fourcc(const std::uint8_t *data, const char *value)
{
    return data[0] == static_cast<std::uint8_t>(value[0]) &&
        data[1] == static_cast<std::uint8_t>(value[1]) &&
        data[2] == static_cast<std::uint8_t>(value[2]) &&
        data[3] == static_cast<std::uint8_t>(value[3]);
}

struct Chunk {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0u;
};

bool applyAlpha(const Chunk& alpha, Image *image, std::string *error)
{
    if (!image || !alpha.data) return true;
    std::vector<std::uint8_t> values;
    if (!WebpAlpha::decode(alpha.data, alpha.size, image->width, image->height, &values, error)) return false;
    const std::size_t pixels = static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height);
    if (values.size() != pixels || image->rgba.size() != pixels * 4u)
        return fail(error, "WebP ALPH dimensions do not match decoded VP8 image");
    image->meaningful_alpha = false;
    for (std::size_t index = 0u; index < pixels; ++index) {
        image->rgba[index * 4u + 3u] = values[index];
        if (values[index] != 255u) image->meaningful_alpha = true;
    }
    return true;
}

} // namespace

bool matches(const std::uint8_t *data, std::size_t size)
{
    return data && size >= 12u && fourcc(data, "RIFF") && fourcc(data + 8u, "WEBP");
}

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null WebP image output");
    if (!matches(data, size)) return fail(error, "invalid WebP RIFF header");

    const std::uint64_t declared = static_cast<std::uint64_t>(u32le(data + 4u)) + 8u;
    if (declared > size || declared < 12u) return fail(error, "WebP RIFF size exceeds input");
    const std::size_t end = static_cast<std::size_t>(declared);

    Chunk vp8l;
    Chunk vp8;
    Chunk alpha;
    bool extended = false;
    bool animated = false;
    bool alpha_flag = false;
    std::uint32_t canvas_width = 0u;
    std::uint32_t canvas_height = 0u;

    std::size_t cursor = 12u;
    while (cursor < end) {
        if (end - cursor < 8u) return fail(error, "truncated WebP chunk header");
        const std::uint8_t *header = data + cursor;
        const std::uint32_t chunk_size = u32le(header + 4u);
        cursor += 8u;
        if (chunk_size > end - cursor) return fail(error, "WebP chunk exceeds RIFF bounds");
        const std::uint8_t *payload = data + cursor;

        if (fourcc(header, "VP8X")) {
            if (chunk_size != 10u) return fail(error, "WebP VP8X chunk must be exactly 10 bytes");
            if (extended) return fail(error, "duplicate WebP VP8X chunk");
            extended = true;
            const std::uint8_t flags = payload[0];
            animated = (flags & 0x02u) != 0u;
            alpha_flag = (flags & 0x10u) != 0u;
            if ((flags & 0xc1u) != 0u || payload[1] != 0u || payload[2] != 0u || payload[3] != 0u)
                return fail(error, "WebP VP8X reserved bits are nonzero");
            canvas_width = u24le(payload + 4u) + 1u;
            canvas_height = u24le(payload + 7u) + 1u;
            if (canvas_width == 0u || canvas_height == 0u ||
                static_cast<std::uint64_t>(canvas_width) * canvas_height > std::numeric_limits<std::uint32_t>::max())
                return fail(error, "invalid WebP VP8X canvas dimensions");
        } else if (fourcc(header, "ALPH")) {
            if (alpha.data) return fail(error, "duplicate WebP ALPH chunk");
            alpha = {payload, chunk_size};
        } else if (fourcc(header, "VP8L")) {
            if (vp8l.data || vp8.data) return fail(error, "multiple WebP image bitstream chunks");
            vp8l = {payload, chunk_size};
        } else if (fourcc(header, "VP8 ")) {
            if (vp8l.data || vp8.data) return fail(error, "multiple WebP image bitstream chunks");
            vp8 = {payload, chunk_size};
        }

        cursor += chunk_size;
        if ((chunk_size & 1u) != 0u) {
            if (cursor >= end) return fail(error, "missing WebP RIFF padding byte");
            if (data[cursor] != 0u) return fail(error, "nonzero WebP RIFF padding byte");
            ++cursor;
        }
    }
    if (cursor != end) return fail(error, "WebP RIFF chunks do not exactly fill container");
    if (animated) return fail(error, "animated WebP is not valid as a static Horse texture");
    if (alpha.data && (!extended || !alpha_flag))
        return fail(error, "WebP ALPH chunk requires the VP8X alpha feature flag");

    if (vp8l.data) {
        if (alpha.data) return fail(error, "WebP VP8L must not have a separate ALPH chunk");
        if (!WebpLossless::decode(vp8l.data, vp8l.size, image, error)) return false;
        if (extended && (image->width != static_cast<int>(canvas_width) || image->height != static_cast<int>(canvas_height)))
            return fail(error, "WebP VP8L dimensions do not match VP8X canvas");
        return true;
    }

    if (vp8.data) {
        if (!WebpVp8::decode(vp8.data, vp8.size, image, error)) return false;
        if (extended && (image->width != static_cast<int>(canvas_width) || image->height != static_cast<int>(canvas_height)))
            return fail(error, "WebP VP8 dimensions do not match VP8X canvas");
        if (alpha_flag && !alpha.data)
            return fail(error, "WebP VP8X alpha flag is set without an ALPH chunk");
        return applyAlpha(alpha, image, error);
    }
    return fail(error, "WebP contains no VP8/VP8L image chunk");
}

} // namespace Models::Images::Webp
