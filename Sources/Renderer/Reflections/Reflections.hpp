#ifndef HORSE_RENDERER_REFLECTIONS_HPP
#define HORSE_RENDERER_REFLECTIONS_HPP

#include "Renderer/Quality.hpp"

#include <cstdint>

namespace Renderer::Reflections {

struct Settings {
    Quality quality = Quality::High;
    float strength = 1.0f;
};

Settings& settings();
const Settings& currentSettings();

void setQuality(Quality value);
Quality quality();

std::uint32_t limitEnvironmentMipLevels(
    std::uint32_t available_levels,
    Quality quality
);
std::uint32_t environmentMipLevels(
    std::uint32_t width,
    std::uint32_t height,
    Quality quality
);
float environmentLod(float roughness, std::uint32_t mip_levels);

} // namespace Renderer::Reflections

#endif
