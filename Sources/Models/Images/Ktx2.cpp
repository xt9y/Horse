#include "Models/Images/Ktx2.hpp"

#include "Models/Compression/Deflate.hpp"
#include "Models/Compression/Zstd.hpp"
#include "Models/Images/Etc1s.hpp"
#include "Models/Images/Uastc.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Models::Images::Ktx2 {
namespace {

constexpr std::array<std::uint8_t, 12> identifier {{
    0xabu, 0x4bu, 0x54u, 0x58u, 0x20u, 0x32u,
    0x30u, 0xbbu, 0x0du, 0x0au, 0x1au, 0x0au,
}};
constexpr std::uint8_t ColorModelEtc1s = 163u;
constexpr std::uint8_t ColorModelUastc = 166u;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint32_t u32(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

std::uint64_t u64(const std::uint8_t *data)
{
    return static_cast<std::uint64_t>(u32(data)) |
        (static_cast<std::uint64_t>(u32(data + 4u)) << 32u);
}

struct Header {
    std::uint32_t format = 0u;
    std::uint32_t type_size = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t depth = 0u;
    std::uint32_t layers = 0u;
    std::uint32_t faces = 0u;
    std::uint32_t levels = 0u;
    std::uint32_t supercompression = 0u;
    std::uint8_t color_model = 0u;
    std::uint64_t sgd_offset = 0u;
    std::uint64_t sgd_length = 0u;
};

struct Level {
    std::uint64_t offset = 0u;
    std::uint64_t length = 0u;
    std::uint64_t uncompressed = 0u;
};

bool checkedRange(std::uint64_t offset, std::uint64_t length, std::size_t size)
{
    return offset <= static_cast<std::uint64_t>(size) &&
        length <= static_cast<std::uint64_t>(size) - offset;
}

bool parse(
    const std::uint8_t *data,
    std::size_t size,
    Header *header,
    Level *base,
    std::string *error)
{
    if (!data || !header || !base || size < 104u) return fail(error, "truncated KTX2 header");
    if (!matches(data, size)) return fail(error, "invalid KTX2 identifier");

    header->format = u32(data + 12u);
    header->type_size = u32(data + 16u);
    header->width = u32(data + 20u);
    header->height = u32(data + 24u);
    header->depth = u32(data + 28u);
    header->layers = u32(data + 32u);
    header->faces = u32(data + 36u);
    header->levels = u32(data + 40u);
    header->supercompression = u32(data + 44u);

    if (header->width == 0u) return fail(error, "KTX2 pixelWidth must be nonzero");
    if (header->faces != 1u && header->faces != 6u) return fail(error, "invalid KTX2 faceCount");
    if (header->type_size == 0u) return fail(error, "KTX2 typeSize must be nonzero");

    const std::uint32_t level_count = std::max(header->levels, 1u);
    if (level_count > (size - 80u) / 24u) return fail(error, "KTX2 level index exceeds file size");
    base->offset = u64(data + 80u);
    base->length = u64(data + 88u);
    base->uncompressed = u64(data + 96u);
    if (!checkedRange(base->offset, base->length, size)) return fail(error, "KTX2 base level exceeds file bounds");

    const std::uint32_t dfd_offset = u32(data + 48u);
    const std::uint32_t dfd_length = u32(data + 52u);
    const std::uint32_t kvd_offset = u32(data + 56u);
    const std::uint32_t kvd_length = u32(data + 60u);
    header->sgd_offset = u64(data + 64u);
    header->sgd_length = u64(data + 72u);
    if (dfd_length != 0u && !checkedRange(dfd_offset, dfd_length, size))
        return fail(error, "KTX2 data format descriptor exceeds file bounds");
    if (kvd_length != 0u && !checkedRange(kvd_offset, kvd_length, size))
        return fail(error, "KTX2 key/value data exceeds file bounds");
    if (header->sgd_length != 0u && !checkedRange(header->sgd_offset, header->sgd_length, size))
        return fail(error, "KTX2 supercompression global data exceeds file bounds");

    if (dfd_length != 0u) {
        if (dfd_length < 28u) return fail(error, "KTX2 basic DFD is truncated");
        const std::uint32_t total_size = u32(data + dfd_offset);
        if (total_size < 28u || total_size > dfd_length)
            return fail(error, "invalid KTX2 DFD total size");
        header->color_model = data[static_cast<std::size_t>(dfd_offset) + 12u];
    } else if (header->format == 0u) {
        return fail(error, "KTX2 transcodable texture has no data format descriptor");
    }
    return true;
}

struct Format {
    int components = 0;
    int bytes_per_component = 0;
    bool bgra = false;
};

Format formatInfo(std::uint32_t format)
{
    switch (format) {
        case 9u:
        case 15u:
            return {1, 1, false};
        case 16u:
        case 22u:
            return {2, 1, false};
        case 23u:
        case 29u:
            return {3, 1, false};
        case 30u:
        case 36u:
            return {3, 1, true};
        case 37u:
        case 43u:
            return {4, 1, false};
        case 44u:
        case 50u:
            return {4, 1, true};
        case 70u:
            return {1, 2, false};
        case 77u:
            return {2, 2, false};
        case 84u:
            return {3, 2, false};
        case 91u:
            return {4, 2, false};
        default:
            return {};
    }
}

std::uint8_t component8(const std::uint8_t *data, int bytes)
{
    if (bytes == 1) return data[0];
    const std::uint16_t value = static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) + 128u) / 257u);
}

bool rgba(
    const Header& header,
    const std::vector<std::uint8_t>& level,
    Image *image,
    std::string *error)
{
    if (!image) return fail(error, "null KTX2 image output");
    if (header.depth > 1u || header.layers > 1u || header.faces != 1u)
        return fail(error, "Horse image textures require a non-array 1D/2D KTX2 image");

    const Format info = formatInfo(header.format);
    if (info.components == 0)
        return fail(error, "unsupported raw KTX2 Vulkan format: " + std::to_string(header.format));
    const std::uint32_t height = std::max(header.height, 1u);
    const std::uint64_t pixels = static_cast<std::uint64_t>(header.width) * height;
    const std::uint64_t texel_bytes = static_cast<std::uint64_t>(info.components) * info.bytes_per_component;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4u ||
        pixels > std::numeric_limits<std::uint64_t>::max() / texel_bytes)
        return fail(error, "KTX2 image dimensions overflow");
    const std::uint64_t required = pixels * texel_bytes;
    if (required > level.size()) return fail(error, "KTX2 base level is shorter than its image dimensions");

    if (header.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return fail(error, "KTX2 dimensions exceed Horse image limits");
    image->width = static_cast<int>(header.width);
    image->height = static_cast<int>(height);
    image->rgba.resize(static_cast<std::size_t>(pixels) * 4u);
    image->meaningful_alpha = false;

    const std::size_t source_stride = static_cast<std::size_t>(texel_bytes);
    for (std::size_t pixel = 0u; pixel < static_cast<std::size_t>(pixels); ++pixel) {
        const std::uint8_t *source = level.data() + pixel * source_stride;
        const auto read = [&](int component) {
            return component8(source + static_cast<std::size_t>(component * info.bytes_per_component), info.bytes_per_component);
        };
        std::uint8_t r = 255u;
        std::uint8_t g = 255u;
        std::uint8_t b = 255u;
        std::uint8_t a = 255u;
        if (info.components == 1) {
            r = g = b = read(0);
        } else if (info.components == 2) {
            r = read(0);
            g = read(1);
            b = 0u;
        } else if (info.components >= 3) {
            if (info.bgra) {
                b = read(0);
                g = read(1);
                r = read(2);
            } else {
                r = read(0);
                g = read(1);
                b = read(2);
            }
            if (info.components == 4) a = read(3);
        }
        const std::size_t destination = pixel * 4u;
        image->rgba[destination + 0u] = r;
        image->rgba[destination + 1u] = g;
        image->rgba[destination + 2u] = b;
        image->rgba[destination + 3u] = a;
        if (a != 255u) image->meaningful_alpha = true;
    }
    return true;
}

bool expectedLevelSize(const Level& level, std::size_t *out, std::string *error)
{
    if (!out) return fail(error, "null KTX2 expected-size output");
    if (level.uncompressed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return fail(error, "KTX2 uncompressed level exceeds addressable memory");
    *out = static_cast<std::size_t>(level.uncompressed);
    return true;
}

bool decompressLevel(
    const Header& header,
    const Level& base,
    const std::uint8_t *level_data,
    std::vector<std::uint8_t> *level,
    std::string *error)
{
    if (!level_data || !level) return fail(error, "invalid KTX2 level output");
    if (base.length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return fail(error, "KTX2 compressed level exceeds addressable memory");
    if (header.supercompression == 0u) {
        level->assign(level_data, level_data + static_cast<std::size_t>(base.length));
        return true;
    }

    std::size_t expected = 0u;
    if (!expectedLevelSize(base, &expected, error)) return false;
    if (header.supercompression == 3u) {
        Compression::InflateOptions options;
        options.max_output = expected == 0u ? 256u * 1024u * 1024u : expected;
        if (!Compression::inflateZlib(
                level_data,
                static_cast<std::size_t>(base.length),
                level,
                error,
                options))
            return false;
        if (expected != 0u && level->size() != expected)
            return fail(error, "KTX2 zlib level size does not match level index");
        return true;
    }
    if (header.supercompression == 2u) {
        Compression::ZstdOptions options;
        options.max_output = expected == 0u ? 256u * 1024u * 1024u : expected;
        options.max_window = std::max<std::size_t>(options.max_output, 8u * 1024u * 1024u);
        if (!Compression::decompressZstd(
                level_data,
                static_cast<std::size_t>(base.length),
                level,
                error,
                options))
            return false;
        if (expected != 0u && level->size() != expected)
            return fail(error, "KTX2 Zstd level size does not match level index");
        return true;
    }
    if (header.supercompression == 1u)
        return fail(error, "KTX2 BasisLZ is decoded by the ETC1S path");
    return fail(error, "unsupported KTX2 supercompression scheme: " + std::to_string(header.supercompression));
}

} // namespace

bool matches(const std::uint8_t *data, std::size_t size)
{
    return data && size >= identifier.size() &&
        std::equal(identifier.begin(), identifier.end(), data);
}

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    Header header;
    Level base;
    if (!parse(data, size, &header, &base, error)) return false;
    if (header.depth > 1u || header.layers > 1u || header.faces != 1u)
        return fail(error, "Horse image textures require a non-array 1D/2D KTX2 image");
    const std::uint32_t height = std::max(header.height, 1u);
    if (header.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return fail(error, "KTX2 dimensions exceed Horse image limits");
    if (base.length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return fail(error, "KTX2 base level exceeds addressable memory");

    const std::uint8_t *level_data = data + static_cast<std::size_t>(base.offset);
    if (header.format == 0u && header.color_model == ColorModelEtc1s) {
        if (header.supercompression != 1u)
            return fail(error, "KTX2 ETC1S requires BasisLZ supercompression");
        if (header.sgd_length == 0u ||
            header.sgd_offset > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            header.sgd_length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            return fail(error, "KTX2 ETC1S is missing addressable BasisLZ global data");
        const std::size_t image_count = static_cast<std::size_t>(std::max(header.levels, 1u));
        return Etc1s::decodeKtx2BaseLevel(
            data + static_cast<std::size_t>(header.sgd_offset),
            static_cast<std::size_t>(header.sgd_length),
            image_count,
            level_data,
            static_cast<std::size_t>(base.length),
            static_cast<int>(header.width),
            static_cast<int>(height),
            image,
            error
        );
    }
    if (header.format == 0u && header.color_model == ColorModelUastc &&
        header.supercompression != 0u && header.supercompression != 2u)
        return fail(error, "KTX2 UASTC permits only NONE or Zstandard supercompression");
    if (header.format == 0u && header.color_model != ColorModelUastc)
        return fail(error, "unsupported KTX2 transcodable DFD color model: " + std::to_string(header.color_model));

    std::vector<std::uint8_t> level;
    if (!decompressLevel(header, base, level_data, &level, error)) return false;
    if (header.format == 0u) {
        return Uastc::decodeImage(
            level.data(),
            level.size(),
            static_cast<int>(header.width),
            static_cast<int>(height),
            image,
            error
        );
    }
    return rgba(header, level, image, error);
}

} // namespace Models::Images::Ktx2
