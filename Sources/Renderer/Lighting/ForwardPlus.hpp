#ifndef HORSE_RENDERER_LIGHTING_FORWARD_PLUS_HPP
#define HORSE_RENDERER_LIGHTING_FORWARD_PLUS_HPP

#include <cstdint>

namespace Renderer::Lighting::ForwardPlus {

inline constexpr std::uint32_t TileSize = 32u;
inline constexpr std::uint32_t MaximumLightsPerTile = 64u;

struct Grid {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    bool operator==(const Grid&) const = default;
};

constexpr Grid grid(std::uint32_t width, std::uint32_t height)
{
    if (width == 0u || height == 0u) return {};
    return {
        (width + TileSize - 1u) / TileSize,
        (height + TileSize - 1u) / TileSize,
    };
}

constexpr std::uint32_t tileIndex(std::uint32_t x, std::uint32_t y, Grid value)
{
    return y * value.width + x;
}

} // namespace Renderer::Lighting::ForwardPlus

#endif
