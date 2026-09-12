#ifndef HORSE_RENDERER_GAUSSIAN_SPLAT_HPP
#define HORSE_RENDERER_GAUSSIAN_SPLAT_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer::GaussianSplat {

bool render(const Ecs::World& world, Internal::FrameOutput& output);
void shutdown();

} // namespace Renderer::GaussianSplat

#endif
