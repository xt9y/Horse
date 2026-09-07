#include "Models/Compression/Checksums.hpp"

namespace Models::Compression {

std::uint32_t adler32(const std::uint8_t *data, std::size_t size)
{
    static constexpr std::uint32_t mod = 65521u;
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    if (!data && size != 0u) return 0u;

    while (size != 0u) {
        const std::size_t block = size > 5552u ? 5552u : size;
        size -= block;
        for (std::size_t i = 0u; i < block; ++i) {
            a += *data++;
            b += a;
        }
        a %= mod;
        b %= mod;
    }
    return (b << 16u) | a;
}

std::uint32_t crc32(const std::uint8_t *data, std::size_t size)
{
    if (!data && size != 0u) return 0u;
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0u; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

} // namespace Models::Compression
