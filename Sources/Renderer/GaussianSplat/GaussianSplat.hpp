#ifndef HORSE_RENDERER_GAUSSIAN_SPLAT_HPP
#define HORSE_RENDERER_GAUSSIAN_SPLAT_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer::GaussianSplat {

struct Settings {
    bool enabled = true;
    float radius = 3.0f;
    float minimum_depth_epsilon = 0.0025f;
    float relative_depth_epsilon = 0.0005f;
};

Settings& settings();
const Settings& currentSettings();

bool render(const Ecs::World& world, Internal::FrameOutput& output);
void shutdown();

} // namespace Renderer::GaussianSplat

#endif
