#include "Models/Compression/XxHash.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Models::Compression {
namespace {

constexpr std::uint64_t Prime1 = 11400714785074694791ull;
constexpr std::uint64_t Prime2 = 14029467366897019727ull;
constexpr std::uint64_t Prime3 = 1609587929392839161ull;
constexpr std::uint64_t Prime4 = 9650029242287828579ull;
constexpr std::uint64_t Prime5 = 2870177450012600261ull;

std::uint64_t rotateLeft(std::uint64_t value, unsigned int count)
{
    return (value << count) | (value >> (64u - count));
}

std::uint32_t read32(const std::uint8_t *data)
{
    std::uint32_t value = 0u;
    std::memcpy(&value, data, sizeof(value));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value = __builtin_bswap32(value);
#endif
    return value;
}

std::uint64_t read64(const std::uint8_t *data)
{
    std::uint64_t value = 0u;
    std::memcpy(&value, data, sizeof(value));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    value = __builtin_bswap64(value);
#endif
    return value;
}

std::uint64_t round(std::uint64_t accumulator, std::uint64_t input)
{
    accumulator += input * Prime2;
    accumulator = rotateLeft(accumulator, 31u);
    accumulator *= Prime1;
    return accumulator;
}

std::uint64_t mergeRound(std::uint64_t accumulator, std::uint64_t value)
{
    accumulator ^= round(0u, value);
    accumulator = accumulator * Prime1 + Prime4;
    return accumulator;
}

} // namespace

std::uint64_t xxHash64(const void *data, std::size_t size, std::uint64_t seed)
{
    const auto *cursor = static_cast<const std::uint8_t *>(data);
    const std::uint8_t *end = cursor ? cursor + size : nullptr;
    if (!cursor && size != 0u) return 0u;

    std::uint64_t hash = 0u;
    if (size >= 32u) {
        std::uint64_t v1 = seed + Prime1 + Prime2;
        std::uint64_t v2 = seed + Prime2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - Prime1;
        const std::uint8_t *limit = end - 32u;
        do {
            v1 = round(v1, read64(cursor)); cursor += 8u;
            v2 = round(v2, read64(cursor)); cursor += 8u;
            v3 = round(v3, read64(cursor)); cursor += 8u;
            v4 = round(v4, read64(cursor)); cursor += 8u;
        } while (cursor <= limit);
        hash = rotateLeft(v1, 1u) + rotateLeft(v2, 7u) + rotateLeft(v3, 12u) + rotateLeft(v4, 18u);
        hash = mergeRound(hash, v1);
        hash = mergeRound(hash, v2);
        hash = mergeRound(hash, v3);
        hash = mergeRound(hash, v4);
    } else {
        hash = seed + Prime5;
    }

    hash += size;
    while (cursor && static_cast<std::size_t>(end - cursor) >= 8u) {
        const std::uint64_t value = round(0u, read64(cursor));
        hash ^= value;
        hash = rotateLeft(hash, 27u) * Prime1 + Prime4;
        cursor += 8u;
    }
    if (cursor && static_cast<std::size_t>(end - cursor) >= 4u) {
        hash ^= static_cast<std::uint64_t>(read32(cursor)) * Prime1;
        hash = rotateLeft(hash, 23u) * Prime2 + Prime3;
        cursor += 4u;
    }
    while (cursor && cursor < end) {
        hash ^= static_cast<std::uint64_t>(*cursor++) * Prime5;
        hash = rotateLeft(hash, 11u) * Prime1;
    }

    hash ^= hash >> 33u;
    hash *= Prime2;
    hash ^= hash >> 29u;
    hash *= Prime3;
    hash ^= hash >> 32u;
    return hash;
}

} // namespace Models::Compression
