#ifndef HORSE_RENDERER_LIGHTING_FORWARD_PLUS_HPP
#define HORSE_RENDERER_LIGHTING_FORWARD_PLUS_HPP

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Renderer::Lighting::ForwardPlus {

inline constexpr std::uint32_t TileSize = 32u;
inline constexpr std::uint32_t MaximumLightsPerTile = 64u;
inline constexpr std::uint32_t OverflowCount = UINT32_MAX;

struct Grid {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    bool operator==(const Grid&) const = default;
};

struct BufferLayout {
    Grid grid{};
    std::size_t tile_count = 0u;
    std::size_t index_count = 0u;
    std::size_t count_bytes = 0u;
    std::size_t index_bytes = 0u;
    bool valid = false;
};

constexpr Grid grid(std::uint32_t width, std::uint32_t height)
{
    if (width == 0u || height == 0u) return {};
    return {
        width / TileSize + (width % TileSize != 0u ? 1u : 0u),
        height / TileSize + (height % TileSize != 0u ? 1u : 0u),
    };
}

constexpr std::uint32_t tileIndex(std::uint32_t x, std::uint32_t y, Grid value)
{
    return y * value.width + x;
}

constexpr BufferLayout bufferLayout(std::uint32_t width, std::uint32_t height)
{
    const Grid tiles = grid(width, height);
    if (tiles.width == 0u || tiles.height == 0u) return {};

    const std::uint64_t tile_count =
        static_cast<std::uint64_t>(tiles.width) * static_cast<std::uint64_t>(tiles.height);
    const std::uint64_t index_count = tile_count * MaximumLightsPerTile;
    constexpr std::uint64_t maximum_size =
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    constexpr std::uint64_t element_size = sizeof(std::uint32_t);

    if (tile_count > maximum_size / element_size ||
        index_count > maximum_size / element_size)
        return {tiles};

    return {
        tiles,
        static_cast<std::size_t>(tile_count),
        static_cast<std::size_t>(index_count),
        static_cast<std::size_t>(tile_count * element_size),
        static_cast<std::size_t>(index_count * element_size),
        true,
    };
}

constexpr bool tileUsesFullLightLoop(std::uint32_t count)
{
    return count == OverflowCount;
}

} // namespace Renderer::Lighting::ForwardPlus

#endif
