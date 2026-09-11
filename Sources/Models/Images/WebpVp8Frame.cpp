#include "Models/Images/WebpVp8Frame.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Models::Images::WebpVp8Internal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint16_t u16le(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
}

std::uint32_t u24le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u);
}

bool bit(BoolDecoder *decoder, bool *value, std::string *error)
{
    if (!decoder || !value) return fail(error, "invalid VP8 bit output");
    unsigned int decoded = 0u;
    if (!decoder->read(128u, &decoded, error)) return false;
    *value = decoded != 0u;
    return true;
}

bool unsignedLiteral(BoolDecoder *decoder, unsigned int width, unsigned int *value, std::string *error)
{
    if (!decoder || !value) return fail(error, "invalid VP8 literal output");
    std::uint32_t decoded = 0u;
    if (!decoder->literal(width, &decoded, error)) return false;
    *value = decoded;
    return true;
}

bool parseSegmentation(BoolDecoder *decoder, Segmentation *segment, std::string *error)
{
    if (!decoder || !segment) return fail(error, "invalid VP8 segmentation output");
    *segment = {};
    if (!bit(decoder, &segment->enabled, error)) return false;
    if (!segment->enabled) return true;

    bool update_data = false;
    if (!bit(decoder, &segment->update_map, error) || !bit(decoder, &update_data, error)) return false;
    if (update_data) {
        if (!bit(decoder, &segment->absolute, error)) return false;
        for (int& value : segment->quantizer)
            if (!decoder->optionalSigned(7u, &value, error)) return false;
        for (int& value : segment->filter)
            if (!decoder->optionalSigned(6u, &value, error)) return false;
    }
    if (segment->update_map) {
        for (std::uint8_t& probability : segment->tree_probability) {
            bool present = false;
            if (!bit(decoder, &present, error)) return false;
            if (present) {
                std::uint32_t value = 0u;
                if (!decoder->literal(8u, &value, error)) return false;
                probability = static_cast<std::uint8_t>(value);
            } else {
                probability = 255u;
            }
        }
    }
    return true;
}

bool parseLoopFilter(BoolDecoder *decoder, LoopFilter *filter, std::string *error)
{
    if (!decoder || !filter) return fail(error, "invalid VP8 loop-filter output");
    *filter = {};
    if (!bit(decoder, &filter->simple, error) ||
        !unsignedLiteral(decoder, 6u, &filter->level, error) ||
        !unsignedLiteral(decoder, 3u, &filter->sharpness, error) ||
        !bit(decoder, &filter->delta_enabled, error))
        return false;
    if (!filter->delta_enabled) return true;

    bool update = false;
    if (!bit(decoder, &update, error)) return false;
    if (!update) return true;
    for (int& value : filter->reference_delta)
        if (!decoder->optionalSigned(6u, &value, error)) return false;
    for (int& value : filter->mode_delta)
        if (!decoder->optionalSigned(6u, &value, error)) return false;
    return true;
}

bool parseQuantizer(BoolDecoder *decoder, Quantizer *quantizer, std::string *error)
{
    if (!decoder || !quantizer) return fail(error, "invalid VP8 quantizer output");
    *quantizer = {};
    if (!unsignedLiteral(decoder, 7u, &quantizer->base, error)) return false;
    return decoder->optionalSigned(4u, &quantizer->y1_dc, error) &&
        decoder->optionalSigned(4u, &quantizer->y2_dc, error) &&
        decoder->optionalSigned(4u, &quantizer->y2_ac, error) &&
        decoder->optionalSigned(4u, &quantizer->uv_dc, error) &&
        decoder->optionalSigned(4u, &quantizer->uv_ac, error);
}

bool parseTokenPartitions(
    BoolDecoder *decoder,
    const std::uint8_t *data,
    std::size_t size,
    std::vector<TokenPartition> *partitions,
    std::string *error)
{
    if (!decoder || !data || !partitions) return fail(error, "invalid VP8 token partition input");
    std::uint32_t log2_count = 0u;
    if (!decoder->literal(2u, &log2_count, error)) return false;
    const std::size_t count = std::size_t{1u} << log2_count;
    const std::size_t table_bytes = (count - 1u) * 3u;
    if (table_bytes > size) return fail(error, "truncated VP8 token partition table");

    partitions->clear();
    partitions->reserve(count);
    std::size_t cursor = table_bytes;
    std::size_t remaining = size - table_bytes;
    for (std::size_t index = 0u; index < count; ++index) {
        std::size_t partition_size = remaining;
        if (index + 1u != count) {
            partition_size = u24le(data + index * 3u);
            if (partition_size > remaining) return fail(error, "VP8 token partition exceeds payload");
        }
        partitions->push_back({data + cursor, partition_size});
        cursor += partition_size;
        remaining -= partition_size;
    }
    if (cursor != size || remaining != 0u) return fail(error, "VP8 token partition sizes do not consume payload");
    return true;
}

} // namespace

bool parseKeyFrame(
    const std::uint8_t *data,
    std::size_t size,
    KeyFrame *frame,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !frame || size < 10u) return fail(error, "truncated VP8 keyframe");
    const std::uint32_t tag = u24le(data);
    if ((tag & 1u) != 0u) return fail(error, "WebP VP8 payload is not a keyframe");
    const unsigned int version = (tag >> 1u) & 7u;
    if (version > 3u) return fail(error, "unsupported VP8 keyframe version");
    const bool shown = ((tag >> 4u) & 1u) != 0u;
    const std::size_t first_partition_size = tag >> 5u;
    if (data[3] != 0x9du || data[4] != 0x01u || data[5] != 0x2au)
        return fail(error, "invalid VP8 keyframe synchronization code");

    const std::uint16_t width_field = u16le(data + 6u);
    const std::uint16_t height_field = u16le(data + 8u);
    const unsigned int width = width_field & 0x3fffu;
    const unsigned int height = height_field & 0x3fffu;
    if (width == 0u || height == 0u ||
        width > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        height > static_cast<unsigned int>(std::numeric_limits<int>::max()))
        return fail(error, "invalid VP8 keyframe dimensions");
    if (first_partition_size > size - 10u)
        return fail(error, "VP8 first partition exceeds payload");
    if (first_partition_size == 0u) return fail(error, "VP8 first partition is empty");

    *frame = {};
    frame->width = static_cast<int>(width);
    frame->height = static_cast<int>(height);
    frame->version = version;
    frame->shown = shown;
    if (!frame->header.reset(data + 10u, first_partition_size, error)) return false;

    unsigned int color_space = 0u;
    unsigned int clamp_type = 0u;
    if (!frame->header.read(128u, &color_space, error) || !frame->header.read(128u, &clamp_type, error)) return false;
    if (color_space != 0u) return fail(error, "unsupported VP8 keyframe color space");
    (void)clamp_type;

    if (!parseSegmentation(&frame->header, &frame->segmentation, error) ||
        !parseLoopFilter(&frame->header, &frame->filter, error))
        return false;

    const std::size_t token_offset = 10u + first_partition_size;
    if (token_offset > size) return fail(error, "invalid VP8 token data offset");
    if (!parseTokenPartitions(
            &frame->header,
            data + token_offset,
            size - token_offset,
            &frame->token_partitions,
            error))
        return false;

    if (!parseQuantizer(&frame->header, &frame->quantizer, error)) return false;
    if (!bit(&frame->header, &frame->refresh_entropy, error)) return false;
    return true;
}

} // namespace Models::Images::WebpVp8Internal
