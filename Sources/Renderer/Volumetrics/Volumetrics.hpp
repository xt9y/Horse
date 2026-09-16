#ifndef HORSE_RENDERER_VOLUMETRICS_VOLUMETRICS_HPP
#define HORSE_RENDERER_VOLUMETRICS_VOLUMETRICS_HPP

#include <cstdint>

namespace Renderer::Volumetrics {

struct Settings {
    bool enabled = true;
    int resolution_divisor = 2;
    std::uint32_t sample_count = 32u;
    std::uint32_t blur_passes = 4u;
    float density = 0.035f;
    float anisotropy = 0.2f;
    float maximum_distance = 120.0f;
    float jitter = 1.0f;
    float depth_falloff = 24.0f;
};

Settings& settings();
const Settings& currentSettings();
void shutdown();

} // namespace Renderer::Volumetrics

#endif
