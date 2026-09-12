#ifndef HORSE_RENDERER_GAUSSIAN_SPLAT_SDLGPU_HPP
#define HORSE_RENDERER_GAUSSIAN_SPLAT_SDLGPU_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer::GaussianSplat::SDLGPU {

bool render(const Ecs::World& world, Internal::FrameOutput& output);
void shutdown();

} // namespace Renderer::GaussianSplat::SDLGPU

#endif
