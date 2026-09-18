#ifndef HORSE_RENDERER_AMBIENT_OCCLUSION_HPP
#define HORSE_RENDERER_AMBIENT_OCCLUSION_HPP

#include "Renderer/Quality.hpp"

#include <cstdint>

namespace Renderer::AmbientOcclusion {

struct Settings {
    Quality quality = Quality::High;
    float strength = 1.0f;
    float radius = 1.0f;
    std::uint32_t sample_count = 8u;
};

Settings& settings();
const Settings& currentSettings();

void setQuality(Quality value);
Quality quality();

} // namespace Renderer::AmbientOcclusion

#endif
