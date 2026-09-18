#ifndef HORSE_RENDERER_VISIBILITY_HIZ_HPP
#define HORSE_RENDERER_VISIBILITY_HIZ_HPP

#include <algorithm>
#include <cstdint>

namespace Renderer::Visibility::HiZ {

struct Extent {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;

    bool operator==(const Extent&) const = default;
};

constexpr std::uint32_t mipCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t maximum = std::max(width, height);
    if (maximum == 0u) return 0u;

    std::uint32_t count = 0u;
    do {
        ++count;
        maximum >>= 1u;
    } while (maximum != 0u);
    return count;
}

constexpr Extent mipExtent(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t level)
{
    if (width == 0u || height == 0u) return {};
    while (level-- != 0u) {
        width = std::max(width >> 1u, 1u);
        height = std::max(height >> 1u, 1u);
    }
    return {width, height};
}

} // namespace Renderer::Visibility::HiZ

#endif
