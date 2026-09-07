#include "Models/Compression/Checksums.hpp"
#include "Models/Compression/Deflate.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main()
{
    using namespace Models::Compression;

    static constexpr std::uint8_t wikipedia[] = {
        'W','i','k','i','p','e','d','i','a'
    };
    assert(adler32(wikipedia, sizeof(wikipedia)) == 0x11e60398u);
    assert(crc32(reinterpret_cast<const std::uint8_t *>("123456789"), 9u) == 0xcbf43926u);

    const std::vector<std::uint8_t> stored = {
        0x78,0x01, 0x01,0x05,0x00,0xfa,0xff,
        'h','e','l','l','o', 0x06,0x2c,0x02,0x15
    };
    std::vector<std::uint8_t> out;
    std::string error;
    assert(inflateZlib(stored.data(), stored.size(), &out, &error));
    assert(std::string(out.begin(), out.end()) == "hello");
    assert(error.empty());

    const std::vector<std::uint8_t> fixed = {
        0x78,0x01,0xcb,0x48,0xcd,0xc9,0xc9,0x57,
        0xc8,0x40,0x27,0x01,0x68,0x03,0x08,0xb1
    };
    out.clear();
    assert(inflateZlib(fixed.data(), fixed.size(), &out, &error));
    assert(std::string(out.begin(), out.end()) == "hello hello hello hello");

    InflateOptions tiny;
    tiny.max_output = 4u;
    out.clear();
    error.clear();
    assert(!inflateZlib(fixed.data(), fixed.size(), &out, &error, tiny));
    assert(error.find("output limit") != std::string::npos);

    // Dynamic-Huffman stream for 5000 bytes with value 'D'.
    const std::vector<std::uint8_t> dynamic = {
        0x78,0xda,0xed,0xc1,0x31,0x01,0x00,0x00,0x00,0xc2,
        0xa0,0x72,0xeb,0x9f,0xc9,0x14,0x7e,0x40,0x01,0x00,
        0x00,0x00,0x00,0x6f,0x03,0x9c,0x59,0x30,0x6c
    };
    out.clear();
    error.clear();
    assert(inflateZlib(dynamic.data(), dynamic.size(), &out, &error));
    assert(out.size() == 5000u);
    for (std::uint8_t byte : out) assert(byte == static_cast<std::uint8_t>('D'));

    std::vector<std::uint8_t> bad_adler = stored;
    bad_adler.back() ^= 0x01u;
    out.clear();
    error.clear();
    assert(!inflateZlib(bad_adler.data(), bad_adler.size(), &out, &error));
    assert(error.find("Adler") != std::string::npos);

    std::vector<std::uint8_t> truncated(stored.begin(), stored.end() - 3);
    out.clear();
    error.clear();
    assert(!inflateZlib(truncated.data(), truncated.size(), &out, &error));
    assert(!error.empty());

    const std::vector<std::uint8_t> invalid_distance = {
        0x78,0x01,0x03,0x02,0x00,0x00,0x00,0x00,0x01
    };
    out.clear();
    error.clear();
    assert(!inflateZlib(
        invalid_distance.data(),
        invalid_distance.size(),
        &out,
        &error
    ));
    assert(!error.empty());

    return 0;
}
