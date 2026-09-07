#include "Models/Compression/Checksums.hpp"
#include "Models/Images/Png.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void be32(Bytes& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

Bytes storedZlib(const Bytes& raw)
{
    Bytes out{0x78u, 0x01u};
    std::size_t offset = 0u;
    while (offset < raw.size() || (raw.empty() && offset == 0u)) {
        const std::size_t remaining = raw.size() - offset;
        const std::size_t chunk = remaining > 65535u ? 65535u : remaining;
        const bool final = offset + chunk == raw.size();
        out.push_back(final ? 0x01u : 0x00u);
        const std::uint16_t length = static_cast<std::uint16_t>(chunk);
        const std::uint16_t inverse = static_cast<std::uint16_t>(length ^ 0xffffu);
        out.push_back(static_cast<std::uint8_t>(length & 0xffu));
        out.push_back(static_cast<std::uint8_t>((length >> 8u) & 0xffu));
        out.push_back(static_cast<std::uint8_t>(inverse & 0xffu));
        out.push_back(static_cast<std::uint8_t>((inverse >> 8u) & 0xffu));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
        if (raw.empty()) break;
    }
    const std::uint32_t checksum = Models::Compression::adler32(raw.data(), raw.size());
    be32(out, checksum);
    return out;
}

void chunk(Bytes& png, const char type[5], const Bytes& payload)
{
    be32(png, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crc_start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    const std::uint32_t checksum = Models::Compression::crc32(png.data() + crc_start, 4u + payload.size());
    be32(png, checksum);
}

Bytes makePng(
    std::uint32_t width,
    std::uint32_t height,
    std::uint8_t bit_depth,
    std::uint8_t color_type,
    std::uint8_t interlace,
    const Bytes& raw,
    const Bytes& palette = {},
    const Bytes& transparency = {})
{
    Bytes png{137u,80u,78u,71u,13u,10u,26u,10u};
    Bytes ihdr;
    be32(ihdr, width);
    be32(ihdr, height);
    ihdr.push_back(bit_depth);
    ihdr.push_back(color_type);
    ihdr.push_back(0u);
    ihdr.push_back(0u);
    ihdr.push_back(interlace);
    chunk(png, "IHDR", ihdr);
    if (!palette.empty()) chunk(png, "PLTE", palette);
    if (!transparency.empty()) chunk(png, "tRNS", transparency);
    chunk(png, "IDAT", storedZlib(raw));
    chunk(png, "IEND", {});
    return png;
}

std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c)
{
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = p > static_cast<int>(a) ? p - static_cast<int>(a) : static_cast<int>(a) - p;
    const int pb = p > static_cast<int>(b) ? p - static_cast<int>(b) : static_cast<int>(b) - p;
    const int pc = p > static_cast<int>(c) ? p - static_cast<int>(c) : static_cast<int>(c) - p;
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

Bytes filtered(std::uint8_t type, const Bytes& current, const Bytes& previous, std::size_t bpp)
{
    Bytes out{type};
    for (std::size_t i = 0u; i < current.size(); ++i) {
        const std::uint8_t left = i >= bpp ? current[i - bpp] : 0u;
        const std::uint8_t up = previous.empty() ? 0u : previous[i];
        const std::uint8_t up_left = (!previous.empty() && i >= bpp) ? previous[i - bpp] : 0u;
        std::uint8_t predictor = 0u;
        if (type == 1u) predictor = left;
        else if (type == 2u) predictor = up;
        else if (type == 3u) predictor = static_cast<std::uint8_t>((static_cast<unsigned>(left) + up) / 2u);
        else if (type == 4u) predictor = paeth(left, up, up_left);
        out.push_back(static_cast<std::uint8_t>(current[i] - predictor));
    }
    return out;
}

} // namespace

int main()
{
    using Models::Images::Image;

    {
        const Bytes png = makePng(1u, 1u, 8u, 6u, 0u, {0u, 10u, 20u, 30u, 128u});
        Image image;
        std::string error;
        assert(Models::Images::Png::matches(png.data(), png.size()));
        assert(Models::Images::Png::decode(png.data(), png.size(), &image, &error));
        assert(image.width == 1 && image.height == 1);
        assert((image.rgba == Bytes{10u,20u,30u,128u}));
        assert(image.meaningful_alpha);

        Bytes corrupt = png;
        corrupt[16] ^= 1u;
        assert(!Models::Images::Png::decode(corrupt.data(), corrupt.size(), &image, &error));
        assert(error.find("CRC") != std::string::npos);
    }

    const Bytes first = {10u,20u,30u, 40u,50u,60u, 70u,80u,90u};
    const Bytes second = {12u,23u,34u, 45u,56u,67u, 78u,89u,100u};
    for (std::uint8_t filter = 0u; filter <= 4u; ++filter) {
        Bytes raw = filtered(0u, first, {}, 3u);
        const Bytes encoded = filtered(filter, second, first, 3u);
        raw.insert(raw.end(), encoded.begin(), encoded.end());
        const Bytes png = makePng(3u, 2u, 8u, 2u, 0u, raw);
        Image image;
        std::string error;
        assert(Models::Images::Png::decode(png.data(), png.size(), &image, &error));
        for (std::size_t i = 0u; i < first.size(); ++i) assert(image.rgba[(i / 3u) * 4u + (i % 3u)] == first[i]);
        const std::size_t row = 3u * 4u;
        for (std::size_t i = 0u; i < second.size(); ++i) assert(image.rgba[row + (i / 3u) * 4u + (i % 3u)] == second[i]);
    }

    {
        const Bytes png = makePng(
            2u, 1u, 1u, 3u, 0u,
            {0u, 0x40u},
            {255u,0u,0u, 0u,255u,0u},
            {255u,0u}
        );
        Image image;
        std::string error;
        assert(Models::Images::Png::decode(png.data(), png.size(), &image, &error));
        assert((image.rgba == Bytes{255u,0u,0u,255u, 0u,255u,0u,0u}));
        assert(image.meaningful_alpha);
    }

    {
        const Bytes png = makePng(1u, 1u, 16u, 0u, 0u, {0u, 0x80u, 0x00u});
        Image image;
        std::string error;
        assert(Models::Images::Png::decode(png.data(), png.size(), &image, &error));
        assert(image.rgba.size() == 4u);
        assert(image.rgba[0] >= 127u && image.rgba[0] <= 128u);
        assert(image.rgba[0] == image.rgba[1] && image.rgba[1] == image.rgba[2]);
        assert(image.rgba[3] == 255u);
    }

    {
        // 2x2 Adam7 RGBA: pass 1=(0,0), pass 6=(1,0), pass 7=(0..1,1).
        const Bytes raw = {
            0u, 255u,0u,0u,255u,
            0u, 0u,255u,0u,255u,
            0u, 0u,0u,255u,255u, 255u,255u,255u,255u
        };
        const Bytes png = makePng(2u, 2u, 8u, 6u, 1u, raw);
        Image image;
        std::string error;
        assert(Models::Images::Png::decode(png.data(), png.size(), &image, &error));
        assert((image.rgba == Bytes{
            255u,0u,0u,255u, 0u,255u,0u,255u,
            0u,0u,255u,255u, 255u,255u,255u,255u
        }));
    }

    return 0;
}
