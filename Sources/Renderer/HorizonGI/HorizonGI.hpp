#ifndef HORSE_RENDERER_HORIZON_GI_HPP
#define HORSE_RENDERER_HORIZON_GI_HPP

#include "Renderer/Quality/ScaledPass.hpp"

namespace Renderer::HorizonGI {

inline constexpr int MaximumDirections = 8;
inline constexpr int MaximumSteps = 16;

struct Settings {
    bool enabled = false;
    Quality::ScaledPassSettings pass{};
    int directions = 4;
    int steps = 6;
    float radius = 1.5f;
    float thickness = 0.15f;
    float ao_strength = 1.0f;
    float indirect_strength = 0.35f;
};

struct Statistics {
    bool active = false;
    bool indirect = false;
    bool temporal_history = false;
    int width = 0;
    int height = 0;
    int directions = 0;
    int steps = 0;
};

Settings sanitized(Settings settings);

} // namespace Renderer::HorizonGI

#endif
