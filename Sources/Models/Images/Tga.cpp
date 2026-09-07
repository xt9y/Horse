#include "Models/Images/Tga.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace Models::Tga {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint16_t read16(const std::uint8_t *bytes)
{
    return static_cast<std::uint16_t>(bytes[0])
        | static_cast<std::uint16_t>(bytes[1]) << 8u;
}

struct Pixel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

bool readPixel(
    const std::vector<std::uint8_t>& bytes,
    std::size_t *cursor,
    std::uint8_t depth,
    Pixel *pixel,
    std::string *error)
{
    const std::size_t count = depth == 32u ? 4u : 3u;
    if (*cursor > bytes.size() || bytes.size() - *cursor < count) {
        return fail(error, "truncated TGA pixel data");
    }

    pixel->b = bytes[*cursor + 0u];
    pixel->g = bytes[*cursor + 1u];
    pixel->r = bytes[*cursor + 2u];
    pixel->a = depth == 32u ? bytes[*cursor + 3u] : 255u;
    *cursor += count;
    return true;
}

void writePixel(
    std::size_t linear,
    std::uint16_t width,
    std::uint16_t height,
    std::uint8_t descriptor,
    const Pixel& pixel,
    Image *image)
{
    const std::size_t source_y = linear / width;
    const std::size_t source_x = linear % width;
    const bool top_origin = (descriptor & 0x20u) != 0u;
    const bool right_origin = (descriptor & 0x10u) != 0u;

    const std::size_t x = right_origin
        ? static_cast<std::size_t>(width) - 1u - source_x
        : source_x;
    const std::size_t y = top_origin
        ? source_y
        : static_cast<std::size_t>(height) - 1u - source_y;
    const std::size_t out = (y * width + x) * 4u;

    image->rgba[out + 0u] = pixel.r;
    image->rgba[out + 1u] = pixel.g;
    image->rgba[out + 2u] = pixel.b;
    image->rgba[out + 3u] = pixel.a;
    if (pixel.a != 255u) image->meaningful_alpha = true;
}

} // namespace

bool load(const std::string& path, Image *image, std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null TGA output image");

    std::ifstream input(path, std::ios::binary);
    if (!input) return fail(error, "failed to open TGA: " + path);

    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size < 18) return fail(error, "truncated TGA header");
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return fail(error, "failed to read TGA: " + path);

    const std::uint8_t id_length = bytes[0];
    const std::uint8_t color_map_type = bytes[1];
    const std::uint8_t image_type = bytes[2];
    const std::uint16_t width = read16(bytes.data() + 12u);
    const std::uint16_t height = read16(bytes.data() + 14u);
    const std::uint8_t depth = bytes[16];
    const std::uint8_t descriptor = bytes[17];

    if (color_map_type != 0u) return fail(error, "color-mapped TGA is not supported by the basic loader");
    if (image_type != 2u && image_type != 10u) return fail(error, "unsupported TGA image type");
    if (width == 0u || height == 0u) return fail(error, "invalid zero-sized TGA");
    if (depth != 24u && depth != 32u) return fail(error, "unsupported TGA pixel depth");

    const std::uint64_t pixel_count_64 = static_cast<std::uint64_t>(width) * height;
    if (pixel_count_64 > std::numeric_limits<std::size_t>::max() / 4u) {
        return fail(error, "TGA dimensions overflow output buffer");
    }
    const std::size_t pixel_count = static_cast<std::size_t>(pixel_count_64);
    std::size_t cursor = 18u + static_cast<std::size_t>(id_length);
    if (cursor > bytes.size()) return fail(error, "truncated TGA image id");

    Image decoded;
    decoded.width = width;
    decoded.height = height;
    decoded.rgba.assign(pixel_count * 4u, 0u);

    std::size_t output = 0u;
    if (image_type == 2u) {
        while (output < pixel_count) {
            Pixel pixel;
            if (!readPixel(bytes, &cursor, depth, &pixel, error)) return false;
            writePixel(output++, width, height, descriptor, pixel, &decoded);
        }
    } else {
        while (output < pixel_count) {
            if (cursor >= bytes.size()) return fail(error, "truncated TGA RLE packet header");
            const std::uint8_t packet = bytes[cursor++];
            const std::size_t count = static_cast<std::size_t>(packet & 0x7fu) + 1u;
            if (count > pixel_count - output) return fail(error, "TGA RLE packet exceeds image bounds");

            if ((packet & 0x80u) != 0u) {
                Pixel pixel;
                if (!readPixel(bytes, &cursor, depth, &pixel, error)) return false;
                for (std::size_t i = 0; i < count; ++i) {
                    writePixel(output++, width, height, descriptor, pixel, &decoded);
                }
            } else {
                for (std::size_t i = 0; i < count; ++i) {
                    Pixel pixel;
                    if (!readPixel(bytes, &cursor, depth, &pixel, error)) return false;
                    writePixel(output++, width, height, descriptor, pixel, &decoded);
                }
            }
        }
    }

    *image = std::move(decoded);
    return true;
}

} // namespace Models::Tga
