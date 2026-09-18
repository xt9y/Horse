#ifndef HORSE_RENDERER_VOLUMETRICS_VOLUMETRICS_HPP
#define HORSE_RENDERER_VOLUMETRICS_VOLUMETRICS_HPP

#include "Renderer/Quality.hpp"

#include <cstdint>

namespace Renderer::Volumetrics {

struct Settings {
    Quality quality = Quality::High;
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

inline void setQuality(Quality value)
{
    Settings& current = settings();
    current.quality = value;
    switch (value) {
    case Quality::Low:
        current.resolution_divisor = 4;
        current.sample_count = 16u;
        current.blur_passes = 2u;
        break;
    case Quality::Medium:
        current.resolution_divisor = 3;
        current.sample_count = 24u;
        current.blur_passes = 3u;
        break;
    case Quality::High:
        current.resolution_divisor = 2;
        current.sample_count = 32u;
        current.blur_passes = 4u;
        break;
    case Quality::Ultra:
        current.resolution_divisor = 1;
        current.sample_count = 48u;
        current.blur_passes = 4u;
        break;
    }
}

inline Quality quality()
{
    return currentSettings().quality;
}

void shutdown();

} // namespace Renderer::Volumetrics

#endif
