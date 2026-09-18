#include "Renderer/Reflections/Reflections.hpp"

#include <algorithm>

namespace Renderer::Reflections {
namespace {

Settings& storage()
{
    static Settings value;
    return value;
}

std::uint32_t maximumMipLevels(Quality quality)
{
    switch (quality) {
        case Quality::Low: return 5u;
        case Quality::Medium: return 7u;
        case Quality::High: return 9u;
        case Quality::Ultra: return UINT32_MAX;
    }
    return 9u;
}

} // namespace

Settings& settings()
{
    return storage();
}

const Settings& currentSettings()
{
    return storage();
}

void setQuality(Quality value)
{
    storage().quality = value;
}

Quality quality()
{
    return storage().quality;
}

std::uint32_t environmentMipLevels(
    std::uint32_t width,
    std::uint32_t height,
    Quality quality)
{
    std::uint32_t size = std::max(width, height);
    if (size == 0u) return 1u;

    std::uint32_t levels = 1u;
    while (size > 1u) {
        size >>= 1u;
        ++levels;
    }
    return std::min(levels, maximumMipLevels(quality));
}

float environmentLod(float roughness, std::uint32_t mip_levels)
{
    if (mip_levels <= 1u) return 0.0f;
    return std::clamp(roughness, 0.0f, 1.0f) *
        static_cast<float>(mip_levels - 1u);
}

} // namespace Renderer::Reflections
