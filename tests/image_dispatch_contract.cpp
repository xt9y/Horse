#include "Models/Compression/Checksums.hpp"
#include "Models/Images/Image.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void be32(Bytes& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24u));
    out.push_back(static_cast<std::uint8_t>(value >> 16u));
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
    out.push_back(static_cast<std::uint8_t>(value));
}

void pngChunk(Bytes& out, const char type[5], const Bytes& payload)
{
    be32(out, static_cast<std::uint32_t>(payload.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    be32(out, Models::Compression::crc32(out.data() + start, 4u + payload.size()));
}

Bytes tinyPng()
{
    Bytes raw{0u, 1u,2u,3u,255u};
    Bytes z{0x78u,0x01u,0x01u,0x05u,0x00u,0xfau,0xffu};
    z.insert(z.end(), raw.begin(), raw.end());
    be32(z, Models::Compression::adler32(raw.data(), raw.size()));
    Bytes out{137u,80u,78u,71u,13u,10u,26u,10u};
    Bytes ihdr; be32(ihdr,1u); be32(ihdr,1u); ihdr.insert(ihdr.end(), {8u,6u,0u,0u,0u});
    pngChunk(out,"IHDR",ihdr); pngChunk(out,"IDAT",z); pngChunk(out,"IEND",{});
    return out;
}

Bytes tinyTga()
{
    Bytes out(18u,0u);
    out[2] = 2u; out[12] = 1u; out[14] = 1u; out[16] = 24u; out[17] = 0x20u;
    out.insert(out.end(), {30u,20u,10u});
    return out;
}

} // namespace

int main()
{
    Models::Images::Image image;
    std::string error;
    const Bytes png = tinyPng();
    assert(Models::Images::loadMemory(png.data(), png.size(), &image, &error));
    assert(image.width == 1 && image.height == 1);
    assert(image.rgba[0] == 1u && image.rgba[1] == 2u && image.rgba[2] == 3u);

    const Bytes tga = tinyTga();
    assert(Models::Images::loadMemory(tga.data(), tga.size(), &image, &error));
    assert(image.rgba[0] == 10u && image.rgba[1] == 20u && image.rgba[2] == 30u);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "rw_signature_dispatch.jpg";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    assert(Models::Images::load(path.string(), &image, &error));
    std::filesystem::remove(path);
    assert(image.width == 1 && image.height == 1);

    const Bytes unknown{1u,2u,3u,4u};
    assert(!Models::Images::loadMemory(unknown.data(), unknown.size(), &image, &error));
    assert(!error.empty());
    return 0;
}
