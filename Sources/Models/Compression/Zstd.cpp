#include "Models/Compression/Zstd.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Models::Compression {
namespace {

constexpr std::uint32_t ZstdMagic = 0xfd2fb528u;
constexpr std::uint32_t SkippableBase = 0x184d2a50u;
constexpr std::uint32_t SkippableMask = 0xfffffff0u;
constexpr std::size_t MaximumBlockSize = 128u * 1024u;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint32_t u24le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u);
}

std::uint32_t u32le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

std::uint64_t unsignedLe(const std::uint8_t *data, std::size_t bytes)
{
    std::uint64_t value = 0u;
    for (std::size_t index = 0u; index < bytes; ++index)
        value |= static_cast<std::uint64_t>(data[index]) << static_cast<unsigned int>(index * 8u);
    return value;
}

bool appendBytes(
    std::vector<std::uint8_t> *output,
    const std::uint8_t *data,
    std::size_t count,
    std::size_t maximum,
    std::string *error)
{
    if (!output || (!data && count != 0u)) return fail(error, "invalid Zstd output/input pointer");
    if (count > maximum - std::min(output->size(), maximum))
        return fail(error, "Zstd output exceeds configured limit");
    output->insert(output->end(), data, data + count);
    return true;
}

bool appendRepeat(
    std::vector<std::uint8_t> *output,
    std::uint8_t value,
    std::size_t count,
    std::size_t maximum,
    std::string *error)
{
    if (!output) return fail(error, "null Zstd output");
    if (count > maximum - std::min(output->size(), maximum))
        return fail(error, "Zstd output exceeds configured limit");
    output->insert(output->end(), count, value);
    return true;
}

struct FrameHeader {
    std::size_t header_bytes = 0u;
    std::uint64_t window_size = 0u;
    std::uint64_t content_size = 0u;
    bool has_content_size = false;
    bool checksum = false;
};

bool parseFrameHeader(
    const std::uint8_t *data,
    std::size_t size,
    FrameHeader *header,
    ZstdOptions options,
    std::string *error)
{
    if (!data || !header || size < 1u) return fail(error, "truncated Zstd frame header");
    const std::uint8_t descriptor = data[0];
    const unsigned int fcs_flag = descriptor >> 6u;
    const bool single_segment = (descriptor & 0x20u) != 0u;
    if ((descriptor & 0x08u) != 0u) return fail(error, "Zstd frame header reserved bit is set");
    header->checksum = (descriptor & 0x04u) != 0u;
    const unsigned int dictionary_flag = descriptor & 0x03u;

    std::size_t cursor = 1u;
    std::uint64_t window_size = 0u;
    if (!single_segment) {
        if (cursor >= size) return fail(error, "truncated Zstd window descriptor");
        const std::uint8_t window = data[cursor++];
        const unsigned int exponent = window >> 3u;
        const unsigned int mantissa = window & 7u;
        const unsigned int window_log = 10u + exponent;
        if (window_log >= 63u) return fail(error, "Zstd window size is too large");
        const std::uint64_t base = std::uint64_t{1u} << window_log;
        window_size = base + (base >> 3u) * mantissa;
    }

    static constexpr std::size_t dictionary_sizes[4] {0u, 1u, 2u, 4u};
    const std::size_t dictionary_size = dictionary_sizes[dictionary_flag];
    if (dictionary_size > size - cursor) return fail(error, "truncated Zstd dictionary id");
    const std::uint64_t dictionary_id = unsignedLe(data + cursor, dictionary_size);
    cursor += dictionary_size;
    if (dictionary_id != 0u) return fail(error, "Zstd dictionaries are not supported by Horse yet");

    std::size_t fcs_size = 0u;
    if (fcs_flag == 0u) fcs_size = single_segment ? 1u : 0u;
    else if (fcs_flag == 1u) fcs_size = 2u;
    else if (fcs_flag == 2u) fcs_size = 4u;
    else fcs_size = 8u;
    if (fcs_size > size - cursor) return fail(error, "truncated Zstd frame content size");

    std::uint64_t content_size = 0u;
    bool has_content_size = fcs_size != 0u;
    if (has_content_size) {
        content_size = unsignedLe(data + cursor, fcs_size);
        if (fcs_size == 2u) content_size += 256u;
        cursor += fcs_size;
    }
    if (single_segment) window_size = content_size;
    if (window_size == 0u && !single_segment)
        return fail(error, "invalid zero Zstd window size");
    if (window_size > options.max_window)
        return fail(error, "Zstd frame window exceeds configured limit");
    if (has_content_size && content_size > options.max_output)
        return fail(error, "Zstd frame content size exceeds configured limit");

    header->header_bytes = cursor;
    header->window_size = window_size;
    header->content_size = content_size;
    header->has_content_size = has_content_size;
    return true;
}

bool decodeCompressedBlock(
    const std::uint8_t *,
    std::size_t,
    std::size_t,
    std::vector<std::uint8_t> *,
    std::string *error)
{
    return fail(error, "Zstd compressed block entropy decoder is not implemented yet");
}

bool decodeFrame(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t *consumed,
    std::vector<std::uint8_t> *output,
    ZstdOptions options,
    std::string *error)
{
    if (!data || !consumed || !output || size < 5u) return fail(error, "truncated Zstd frame");
    if (u32le(data) != ZstdMagic) return fail(error, "invalid Zstd frame magic");

    FrameHeader header;
    if (!parseFrameHeader(data + 4u, size - 4u, &header, options, error)) return false;
    std::size_t cursor = 4u + header.header_bytes;
    const std::size_t frame_output_begin = output->size();
    const std::size_t block_maximum = static_cast<std::size_t>(std::min<std::uint64_t>(
        header.window_size,
        MaximumBlockSize
    ));
    if (block_maximum == 0u && (!header.has_content_size || header.content_size != 0u))
        return fail(error, "invalid Zstd block maximum size");

    bool last = false;
    do {
        if (size - cursor < 3u) return fail(error, "truncated Zstd block header");
        const std::uint32_t block_header = u24le(data + cursor);
        cursor += 3u;
        last = (block_header & 1u) != 0u;
        const unsigned int type = (block_header >> 1u) & 3u;
        const std::size_t block_size = block_header >> 3u;
        if (type == 3u) return fail(error, "reserved Zstd block type");
        if (block_size > block_maximum)
            return fail(error, "Zstd block exceeds frame block maximum");

        if (type == 0u) {
            if (block_size > size - cursor) return fail(error, "truncated Zstd raw block");
            if (!appendBytes(output, data + cursor, block_size, options.max_output, error)) return false;
            cursor += block_size;
        } else if (type == 1u) {
            if (cursor >= size) return fail(error, "truncated Zstd RLE block");
            if (!appendRepeat(output, data[cursor], block_size, options.max_output, error)) return false;
            ++cursor;
        } else {
            if (block_size > size - cursor) return fail(error, "truncated Zstd compressed block");
            if (!decodeCompressedBlock(data + cursor, block_size, block_maximum, output, error)) return false;
            cursor += block_size;
        }
    } while (!last);

    if (header.checksum) {
        if (size - cursor < 4u) return fail(error, "truncated Zstd content checksum");
        cursor += 4u;
    }

    const std::size_t regenerated = output->size() - frame_output_begin;
    if (header.has_content_size && regenerated != header.content_size)
        return fail(error, "Zstd regenerated size does not match frame content size");
    *consumed = cursor;
    return true;
}

} // namespace

bool decompressZstd(
    const std::uint8_t *data,
    std::size_t size,
    std::vector<std::uint8_t> *output,
    std::string *error,
    ZstdOptions options)
{
    if (error) error->clear();
    if (!output) return fail(error, "null Zstd output");
    output->clear();
    if (!data && size != 0u) return fail(error, "null Zstd input");
    if (options.max_output == 0u || options.max_window == 0u)
        return fail(error, "Zstd limits must be nonzero");

    std::size_t cursor = 0u;
    while (cursor < size) {
        if (size - cursor < 4u) return fail(error, "truncated Zstd frame magic");
        const std::uint32_t magic = u32le(data + cursor);
        if ((magic & SkippableMask) == SkippableBase) {
            if (size - cursor < 8u) return fail(error, "truncated Zstd skippable frame");
            const std::size_t payload = u32le(data + cursor + 4u);
            if (payload > size - cursor - 8u) return fail(error, "truncated Zstd skippable frame payload");
            cursor += 8u + payload;
            continue;
        }
        if (magic != ZstdMagic) return fail(error, "invalid Zstd frame magic");
        std::size_t consumed = 0u;
        if (!decodeFrame(data + cursor, size - cursor, &consumed, output, options, error)) return false;
        if (consumed == 0u || consumed > size - cursor) return fail(error, "invalid Zstd frame length");
        cursor += consumed;
    }
    return true;
}

} // namespace Models::Compression
