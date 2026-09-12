#ifndef HORSE_RENDERER_GAUSSIAN_SPLAT_METAL_HPP
#define HORSE_RENDERER_GAUSSIAN_SPLAT_METAL_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer::GaussianSplat::Metal {

bool render(const Ecs::World& world, Internal::FrameOutput& output);
void shutdown();

} // namespace Renderer::GaussianSplat::Metal

#endif
