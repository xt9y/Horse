#include "Models/Images/Png.hpp"

#include "Models/Compression/Checksums.hpp"
#include "Models/Compression/Deflate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Models::Images::Png {
namespace {

#if defined(_MSC_VER)
#define HORSE_PNG_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define HORSE_PNG_INLINE inline __attribute__((always_inline))
#else
#define HORSE_PNG_INLINE inline
#endif

constexpr std::array<std::uint8_t, 8> kSignature = {
    137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
};
constexpr std::size_t kMaximumPixels = 268435456u;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool checkedAdd(std::size_t a, std::size_t b, std::size_t *out)
{
    if (!out || b > std::numeric_limits<std::size_t>::max() - a) return false;
    *out = a + b;
    return true;
}

std::uint32_t readBe32(const std::uint8_t *data)
{
    return
        (static_cast<std::uint32_t>(data[0]) << 24u) |
        (static_cast<std::uint32_t>(data[1]) << 16u) |
        (static_cast<std::uint32_t>(data[2]) << 8u) |
        static_cast<std::uint32_t>(data[3]);
}

std::uint16_t readBe16(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8u) |
        static_cast<std::uint16_t>(data[1])
    );
}

constexpr std::array<std::uint32_t, 256> makeCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t value = 0u; value < table.size(); ++value) {
        std::uint32_t crc = value;
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        table[value] = crc;
    }
    return table;
}

constexpr auto kCrcTable = makeCrcTable();

HORSE_PNG_INLINE std::uint32_t crcByte(std::uint32_t crc, std::uint8_t value)
{
    return kCrcTable.data()[(crc ^ value) & 0xffu] ^ (crc >> 8u);
}

std::uint32_t chunkCrc(const std::uint8_t *type, const std::uint8_t *data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffu;
    crc = crcByte(crc, type[0]);
    crc = crcByte(crc, type[1]);
    crc = crcByte(crc, type[2]);
    crc = crcByte(crc, type[3]);
    for (std::size_t i = 0u; i < size; ++i) crc = crcByte(crc, data[i]);
    return ~crc;
}

bool typeEquals(const std::uint8_t *type, const char literal[5])
{
    return std::memcmp(type, literal, 4u) == 0;
}

int channelsFor(std::uint8_t color_type)
{
    switch (color_type) {
        case 0u: return 1;
        case 2u: return 3;
        case 3u: return 1;
        case 4u: return 2;
        case 6u: return 4;
        default: return 0;
    }
}

bool legalFormat(std::uint8_t color_type, std::uint8_t bit_depth)
{
    switch (color_type) {
        case 0u: return bit_depth == 1u || bit_depth == 2u || bit_depth == 4u || bit_depth == 8u || bit_depth == 16u;
        case 2u: return bit_depth == 8u || bit_depth == 16u;
        case 3u: return bit_depth == 1u || bit_depth == 2u || bit_depth == 4u || bit_depth == 8u;
        case 4u: return bit_depth == 8u || bit_depth == 16u;
        case 6u: return bit_depth == 8u || bit_depth == 16u;
        default: return false;
    }
}

std::size_t passExtent(std::size_t full, std::size_t start, std::size_t step)
{
    if (full <= start) return 0u;
    return 1u + (full - 1u - start) / step;
}

HORSE_PNG_INLINE std::uint8_t paethPredictor(
    std::uint8_t left,
    std::uint8_t above,
    std::uint8_t upper_left)
{
    const int a = left;
    const int b = above;
    const int c = upper_left;
    const int ac = a - c;
    const int bc = b - c;
    const int distance_a = bc < 0 ? -bc : bc;
    const int distance_b = ac < 0 ? -ac : ac;
    const int acbc = ac + bc;
    const int distance_c = acbc < 0 ? -acbc : acbc;
    return static_cast<std::uint8_t>(
        distance_a <= distance_b && distance_a <= distance_c
            ? a
            : (distance_b <= distance_c ? b : c));
}

bool unfilterRow(
    std::uint8_t filter,
    const std::uint8_t *source,
    std::size_t row_bytes,
    const std::vector<std::uint8_t>& previous,
    std::size_t bpp,
    std::vector<std::uint8_t> *row,
    std::string *error)
{
    if (!source || !row) return fail(error, "invalid PNG scanline state");
    if (filter > 4u) return fail(error, "invalid PNG filter type");
    row->resize(row_bytes);
    std::uint8_t *destination = row->data();
    if (previous.size() < row_bytes) return fail(error, "invalid PNG previous scanline size");
    const std::uint8_t *up = previous.data();

    if (filter == 0u) {
        std::memcpy(destination, source, row_bytes);
        return true;
    }
    if (filter == 1u) {
        const std::size_t prefix = std::min(bpp, row_bytes);
        std::memcpy(destination, source, prefix);
        for (std::size_t i = prefix; i < row_bytes; ++i)
            destination[i] = static_cast<std::uint8_t>(source[i] + destination[i - bpp]);
        return true;
    }
    if (filter == 2u) {
        std::size_t i = 0u;
        for (; i + 8u <= row_bytes; i += 8u) {
            destination[i + 0u] = static_cast<std::uint8_t>(source[i + 0u] + up[i + 0u]);
            destination[i + 1u] = static_cast<std::uint8_t>(source[i + 1u] + up[i + 1u]);
            destination[i + 2u] = static_cast<std::uint8_t>(source[i + 2u] + up[i + 2u]);
            destination[i + 3u] = static_cast<std::uint8_t>(source[i + 3u] + up[i + 3u]);
            destination[i + 4u] = static_cast<std::uint8_t>(source[i + 4u] + up[i + 4u]);
            destination[i + 5u] = static_cast<std::uint8_t>(source[i + 5u] + up[i + 5u]);
            destination[i + 6u] = static_cast<std::uint8_t>(source[i + 6u] + up[i + 6u]);
            destination[i + 7u] = static_cast<std::uint8_t>(source[i + 7u] + up[i + 7u]);
        }
        for (; i < row_bytes; ++i)
            destination[i] = static_cast<std::uint8_t>(source[i] + up[i]);
        return true;
    }
    if (filter == 3u) {
        for (std::size_t i = 0u; i < row_bytes; ++i) {
            const unsigned left = i >= bpp ? destination[i - bpp] : 0u;
            const unsigned above = up[i];
            destination[i] = static_cast<std::uint8_t>(source[i] + ((left + above) >> 1u));
        }
        return true;
    }

    const std::size_t prefix = std::min(bpp, row_bytes);
    for (std::size_t i = 0u; i < prefix; ++i)
        destination[i] = static_cast<std::uint8_t>(source[i] + up[i]);

    if (bpp == 4u) {
        for (std::size_t i = 4u; i + 3u < row_bytes; i += 4u) {
            destination[i + 0u] = static_cast<std::uint8_t>(source[i + 0u] + paethPredictor(destination[i - 4u], up[i + 0u], up[i - 4u]));
            destination[i + 1u] = static_cast<std::uint8_t>(source[i + 1u] + paethPredictor(destination[i - 3u], up[i + 1u], up[i - 3u]));
            destination[i + 2u] = static_cast<std::uint8_t>(source[i + 2u] + paethPredictor(destination[i - 2u], up[i + 2u], up[i - 2u]));
            destination[i + 3u] = static_cast<std::uint8_t>(source[i + 3u] + paethPredictor(destination[i - 1u], up[i + 3u], up[i - 1u]));
        }
        return true;
    }
    if (bpp == 3u) {
        for (std::size_t i = 3u; i + 2u < row_bytes; i += 3u) {
            destination[i + 0u] = static_cast<std::uint8_t>(source[i + 0u] + paethPredictor(destination[i - 3u], up[i + 0u], up[i - 3u]));
            destination[i + 1u] = static_cast<std::uint8_t>(source[i + 1u] + paethPredictor(destination[i - 2u], up[i + 1u], up[i - 2u]));
            destination[i + 2u] = static_cast<std::uint8_t>(source[i + 2u] + paethPredictor(destination[i - 1u], up[i + 2u], up[i - 1u]));
        }
        return true;
    }

    for (std::size_t i = prefix; i < row_bytes; ++i) {
        destination[i] = static_cast<std::uint8_t>(
            source[i] + paethPredictor(destination[i - bpp], up[i], up[i - bpp]));
    }
    return true;
}

std::uint16_t sampleAt(
    const std::vector<std::uint8_t>& row,
    std::size_t sample_index,
    std::uint8_t bit_depth)
{
    if (bit_depth == 16u) {
        const std::size_t offset = sample_index * 2u;
        return readBe16(row.data() + offset);
    }
    if (bit_depth == 8u) return row[sample_index];
    const std::size_t bit = sample_index * bit_depth;
    const std::size_t byte_index = bit >> 3u;
    const unsigned bit_in_byte = static_cast<unsigned>(bit & 7u);
    const unsigned shift = 8u - static_cast<unsigned>(bit_depth) - bit_in_byte;
    const std::uint16_t mask = static_cast<std::uint16_t>((1u << bit_depth) - 1u);
    return static_cast<std::uint16_t>((row[byte_index] >> shift) & mask);
}

std::uint8_t scaleSample(std::uint16_t value, std::uint8_t bit_depth)
{
    if (bit_depth == 8u) return static_cast<std::uint8_t>(value);
    if (bit_depth == 16u) {
        const std::uint32_t scaled = (static_cast<std::uint32_t>(value) * 255u + 32767u) / 65535u;
        return static_cast<std::uint8_t>(scaled);
    }
    const std::uint32_t maximum = (1u << bit_depth) - 1u;
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(value) * 255u + maximum / 2u) / maximum);
}

struct DecodeState {
    std::size_t width = 0u;
    std::size_t height = 0u;
    std::uint8_t bit_depth = 0u;
    std::uint8_t color_type = 0u;
    std::uint8_t interlace = 0u;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> transparency;
};

bool writeRow8(
    const DecodeState& state,
    const std::vector<std::uint8_t>& row,
    std::size_t destination_y,
    Image *image,
    std::string *error)
{
    if (!image || destination_y >= state.height) return fail(error, "PNG row destination overflow");
    const std::size_t destination_offset = destination_y * state.width * 4u;
    if (destination_offset > image->rgba.size() ||
        state.width * 4u > image->rgba.size() - destination_offset)
    {
        return fail(error, "PNG row destination overflow");
    }

    std::uint8_t *destination = image->rgba.data() + destination_offset;
    if (state.color_type == 6u) {
        if (row.size() != state.width * 4u) return fail(error, "invalid PNG RGBA row size");
        std::memcpy(destination, row.data(), row.size());
        if (!image->meaningful_alpha) {
            for (std::size_t source = 3u; source < row.size(); source += 4u) {
                if (row[source] != 255u) {
                    image->meaningful_alpha = true;
                    break;
                }
            }
        }
        return true;
    }

    if (state.color_type == 2u) {
        if (row.size() != state.width * 3u) return fail(error, "invalid PNG RGB row size");
        const bool transparent = state.transparency.size() == 6u;
        const std::uint16_t transparent_red = transparent ? readBe16(state.transparency.data()) : 0u;
        const std::uint16_t transparent_green = transparent ? readBe16(state.transparency.data() + 2u) : 0u;
        const std::uint16_t transparent_blue = transparent ? readBe16(state.transparency.data() + 4u) : 0u;
        const std::uint8_t *source = row.data();
        if (!transparent) {
            std::size_t x = 0u;
            for (; x + 4u <= state.width; x += 4u, source += 12u, destination += 16u) {
                destination[0] = source[0];
                destination[1] = source[1];
                destination[2] = source[2];
                destination[3] = 255u;
                destination[4] = source[3];
                destination[5] = source[4];
                destination[6] = source[5];
                destination[7] = 255u;
                destination[8] = source[6];
                destination[9] = source[7];
                destination[10] = source[8];
                destination[11] = 255u;
                destination[12] = source[9];
                destination[13] = source[10];
                destination[14] = source[11];
                destination[15] = 255u;
            }
            for (; x < state.width; ++x, source += 3u, destination += 4u) {
                destination[0] = source[0];
                destination[1] = source[1];
                destination[2] = source[2];
                destination[3] = 255u;
            }
            return true;
        }
        for (std::size_t x = 0u; x < state.width; ++x, source += 3u, destination += 4u) {
            destination[0] = source[0];
            destination[1] = source[1];
            destination[2] = source[2];
            const bool clear =
                source[0] == transparent_red &&
                source[1] == transparent_green &&
                source[2] == transparent_blue;
            destination[3] = clear ? 0u : 255u;
            image->meaningful_alpha = image->meaningful_alpha || clear;
        }
        return true;
    }

    return false;
}

bool writePixel(
    const DecodeState& state,
    const std::vector<std::uint8_t>& row,
    std::size_t source_x,
    std::size_t destination_x,
    std::size_t destination_y,
    Image *image,
    std::string *error)
{
    const int channels = channelsFor(state.color_type);
    const std::size_t sample = source_x * static_cast<std::size_t>(channels);
    std::array<std::uint8_t, 4> rgba{0u,0u,0u,255u};

    if (state.color_type == 0u) {
        const std::uint16_t gray = sampleAt(row, sample, state.bit_depth);
        const std::uint8_t value = scaleSample(gray, state.bit_depth);
        rgba = {value, value, value, 255u};
        if (state.transparency.size() == 2u && gray == readBe16(state.transparency.data())) rgba[3] = 0u;
    } else if (state.color_type == 2u) {
        const std::uint16_t red = sampleAt(row, sample + 0u, state.bit_depth);
        const std::uint16_t green = sampleAt(row, sample + 1u, state.bit_depth);
        const std::uint16_t blue = sampleAt(row, sample + 2u, state.bit_depth);
        rgba = {
            scaleSample(red, state.bit_depth),
            scaleSample(green, state.bit_depth),
            scaleSample(blue, state.bit_depth),
            255u
        };
        if (state.transparency.size() == 6u &&
            red == readBe16(state.transparency.data()) &&
            green == readBe16(state.transparency.data() + 2u) &&
            blue == readBe16(state.transparency.data() + 4u))
        {
            rgba[3] = 0u;
        }
    } else if (state.color_type == 3u) {
        const std::uint16_t index = sampleAt(row, source_x, state.bit_depth);
        const std::size_t palette_offset = static_cast<std::size_t>(index) * 3u;
        if (palette_offset + 2u >= state.palette.size()) return fail(error, "PNG palette index out of range");
        rgba = {
            state.palette[palette_offset],
            state.palette[palette_offset + 1u],
            state.palette[palette_offset + 2u],
            index < state.transparency.size() ? state.transparency[index] : static_cast<std::uint8_t>(255u)
        };
    } else if (state.color_type == 4u) {
        const std::uint16_t gray = sampleAt(row, sample, state.bit_depth);
        const std::uint8_t value = scaleSample(gray, state.bit_depth);
        rgba = {value, value, value, scaleSample(sampleAt(row, sample + 1u, state.bit_depth), state.bit_depth)};
    } else if (state.color_type == 6u) {
        rgba = {
            scaleSample(sampleAt(row, sample + 0u, state.bit_depth), state.bit_depth),
            scaleSample(sampleAt(row, sample + 1u, state.bit_depth), state.bit_depth),
            scaleSample(sampleAt(row, sample + 2u, state.bit_depth), state.bit_depth),
            scaleSample(sampleAt(row, sample + 3u, state.bit_depth), state.bit_depth)
        };
    } else {
        return fail(error, "unsupported PNG color type");
    }

    const std::size_t destination = (destination_y * state.width + destination_x) * 4u;
    if (destination + 4u > image->rgba.size()) return fail(error, "PNG pixel destination overflow");
    std::copy(rgba.begin(), rgba.end(), image->rgba.begin() + static_cast<std::ptrdiff_t>(destination));
    if (rgba[3] != 255u) image->meaningful_alpha = true;
    return true;
}

bool decodePass(
    const DecodeState& state,
    const std::uint8_t *inflated,
    std::size_t inflated_size,
    std::size_t *offset,
    std::size_t x_start,
    std::size_t y_start,
    std::size_t x_step,
    std::size_t y_step,
    Image *image,
    std::string *error)
{
    const std::size_t pass_width = passExtent(state.width, x_start, x_step);
    const std::size_t pass_height = passExtent(state.height, y_start, y_step);
    if (pass_width == 0u || pass_height == 0u) return true;

    const std::size_t bits_per_pixel = static_cast<std::size_t>(channelsFor(state.color_type)) * state.bit_depth;
    if (pass_width > (std::numeric_limits<std::size_t>::max() - 7u) / bits_per_pixel) {
        return fail(error, "PNG row size overflow");
    }
    const std::size_t row_bytes = (pass_width * bits_per_pixel + 7u) / 8u;
    const std::size_t bpp = std::max<std::size_t>(1u, (bits_per_pixel + 7u) / 8u);

    std::vector<std::uint8_t> previous(row_bytes, 0u);
    std::vector<std::uint8_t> row(row_bytes, 0u);
    for (std::size_t y = 0u; y < pass_height; ++y) {
        if (*offset >= inflated_size) return fail(error, "truncated PNG scanline filter");
        const std::uint8_t filter = inflated[(*offset)++];
        if (row_bytes > inflated_size - *offset) return fail(error, "truncated PNG scanline");
        if (!unfilterRow(filter, inflated + *offset, row_bytes, previous, bpp, &row, error)) return false;
        *offset += row_bytes;

        const bool direct_8bit_row =
            state.bit_depth == 8u &&
            x_start == 0u && y_start == 0u &&
            x_step == 1u && y_step == 1u &&
            (state.color_type == 2u || state.color_type == 6u);
        if (direct_8bit_row) {
            if (!writeRow8(state, row, y, image, error)) return false;
        } else {
            for (std::size_t x = 0u; x < pass_width; ++x) {
                if (!writePixel(
                    state,
                    row,
                    x,
                    x_start + x * x_step,
                    y_start + y * y_step,
                    image,
                    error
                )) return false;
            }
        }
        previous.swap(row);
    }
    return true;
}

bool expectedInflatedSize(const DecodeState& state, std::size_t *result, std::string *error)
{
    static constexpr std::array<std::array<std::size_t,4>,7> passes = {{
        {{0u,0u,8u,8u}}, {{4u,0u,8u,8u}}, {{0u,4u,4u,8u}},
        {{2u,0u,4u,4u}}, {{0u,2u,2u,4u}}, {{1u,0u,2u,2u}}, {{0u,1u,1u,2u}}
    }};
    const std::size_t bits_per_pixel = static_cast<std::size_t>(channelsFor(state.color_type)) * state.bit_depth;
    std::size_t total = 0u;

    const auto add_pass = [&](std::size_t xs, std::size_t ys, std::size_t xstep, std::size_t ystep, std::size_t *target) -> bool {
        const std::size_t pw = passExtent(state.width, xs, xstep);
        const std::size_t ph = passExtent(state.height, ys, ystep);
        if (pw == 0u || ph == 0u) return true;
        if (pw > (std::numeric_limits<std::size_t>::max() - 7u) / bits_per_pixel) return false;
        const std::size_t row = (pw * bits_per_pixel + 7u) / 8u;
        if (row == std::numeric_limits<std::size_t>::max()) return false;
        const std::size_t per_row = row + 1u;
        if (ph > std::numeric_limits<std::size_t>::max() / per_row) return false;
        const std::size_t amount = ph * per_row;
        return checkedAdd(*target, amount, target);
    };

    if (state.interlace == 0u) {
        if (!add_pass(0u,0u,1u,1u,&total)) return fail(error, "PNG inflated size overflow");
    } else {
        for (const auto& pass : passes) {
            if (!add_pass(pass[0],pass[1],pass[2],pass[3],&total)) return fail(error, "PNG inflated size overflow");
        }
    }
    *result = total;
    return true;
}

#undef HORSE_PNG_INLINE

} // namespace

bool matches(const std::uint8_t *data, std::size_t size)
{
    return data && size >= kSignature.size() &&
        std::memcmp(data, kSignature.data(), kSignature.size()) == 0;
}

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error)
{
    if (error) error->clear();
    if (!image) return fail(error, "null PNG image output");
    *image = {};
    if (!matches(data, size)) return fail(error, "invalid PNG signature");

    DecodeState state;
    bool have_ihdr = false;
    bool have_iend = false;
    bool seen_idat = false;
    std::string idat;
    std::size_t offset = kSignature.size();

    while (offset < size) {
        if (size - offset < 12u) return fail(error, "truncated PNG chunk header");
        const std::uint32_t length32 = readBe32(data + offset);
        offset += 4u;
        const std::size_t length = length32;
        if (length > size - offset - 8u) return fail(error, "PNG chunk length exceeds input");
        const std::uint8_t *type = data + offset;
        offset += 4u;
        const std::uint8_t *payload = data + offset;
        offset += length;
        const std::uint32_t stored_crc = readBe32(data + offset);
        offset += 4u;
        if (chunkCrc(type, payload, length) != stored_crc) return fail(error, "PNG chunk CRC mismatch");

        if (typeEquals(type, "IHDR")) {
            if (have_ihdr || seen_idat || length != 13u) return fail(error, "invalid PNG IHDR placement or length");
            state.width = readBe32(payload);
            state.height = readBe32(payload + 4u);
            state.bit_depth = payload[8];
            state.color_type = payload[9];
            const std::uint8_t compression = payload[10];
            const std::uint8_t filter = payload[11];
            state.interlace = payload[12];
            if (state.width == 0u || state.height == 0u) return fail(error, "PNG has zero dimensions");
            if (state.width > kMaximumPixels || state.height > kMaximumPixels || state.height > kMaximumPixels / state.width) {
                return fail(error, "PNG dimensions exceed image limit");
            }
            if (!legalFormat(state.color_type, state.bit_depth)) return fail(error, "unsupported PNG bit depth/color type combination");
            if (compression != 0u || filter != 0u || state.interlace > 1u) return fail(error, "unsupported PNG compression/filter/interlace method");
            have_ihdr = true;
        } else if (typeEquals(type, "PLTE")) {
            if (!have_ihdr || seen_idat || length == 0u || length % 3u != 0u || length > 768u) return fail(error, "invalid PNG PLTE chunk");
            state.palette.assign(payload, payload + length);
        } else if (typeEquals(type, "tRNS")) {
            if (!have_ihdr || seen_idat) return fail(error, "invalid PNG tRNS placement");
            if ((state.color_type == 0u && length != 2u) ||
                (state.color_type == 2u && length != 6u) ||
                (state.color_type == 3u && length > 256u) ||
                state.color_type == 4u || state.color_type == 6u)
            {
                return fail(error, "invalid PNG tRNS chunk");
            }
            state.transparency.assign(payload, payload + length);
        } else if (typeEquals(type, "IDAT")) {
            if (!have_ihdr || have_iend) return fail(error, "invalid PNG IDAT placement");
            std::size_t new_size = 0u;
            if (!checkedAdd(idat.size(), length, &new_size) || new_size > 1024u * 1024u * 1024u) {
                return fail(error, "PNG IDAT data exceeds limit");
            }
            idat.append(reinterpret_cast<const char *>(payload), length);
            seen_idat = true;
        } else if (typeEquals(type, "IEND")) {
            if (!have_ihdr || !seen_idat || length != 0u) return fail(error, "invalid PNG IEND chunk");
            have_iend = true;
            break;
        } else if ((type[0] & 0x20u) == 0u) {
            return fail(error, "unsupported critical PNG chunk");
        }
    }

    if (!have_ihdr || !seen_idat || !have_iend) return fail(error, "incomplete PNG stream");
    if (state.color_type == 3u && state.palette.empty()) return fail(error, "indexed PNG has no palette");
    if (state.color_type == 3u && state.transparency.size() > state.palette.size() / 3u) {
        return fail(error, "PNG transparency table exceeds palette");
    }

    std::size_t expected = 0u;
    if (!expectedInflatedSize(state, &expected, error)) return false;
    Compression::InflateOptions inflate_options;
    inflate_options.max_output = expected;
    std::unique_ptr<std::uint8_t[]> inflated = std::make_unique<std::uint8_t[]>(expected);
    if (!Compression::inflateZlibExact(
            reinterpret_cast<const std::uint8_t *>(idat.data()),
            idat.size(),
            inflated.get(),
            expected,
            error,
            inflate_options))
        return false;

    image->width = static_cast<int>(state.width);
    image->height = static_cast<int>(state.height);
    image->rgba.assign(state.width * state.height * 4u, 0u);
    image->meaningful_alpha = false;

    std::size_t scan_offset = 0u;
    if (state.interlace == 0u) {
        if (!decodePass(state, inflated.get(), expected, &scan_offset, 0u,0u,1u,1u, image, error)) return false;
    } else {
        static constexpr std::array<std::array<std::size_t,4>,7> passes = {{
            {{0u,0u,8u,8u}}, {{4u,0u,8u,8u}}, {{0u,4u,4u,8u}},
            {{2u,0u,4u,4u}}, {{0u,2u,2u,4u}}, {{1u,0u,2u,2u}}, {{0u,1u,1u,2u}}
        }};
        for (const auto& pass : passes) {
            if (!decodePass(state, inflated.get(), expected, &scan_offset, pass[0],pass[1],pass[2],pass[3], image, error)) return false;
        }
    }
    if (scan_offset != expected) return fail(error, "PNG scanline data has trailing bytes");
    return true;
}

} // namespace Models::Images::Png
