#ifndef HORSE_RENDERER_GAUSSIAN_SPLAT_OPENGL_HPP
#define HORSE_RENDERER_GAUSSIAN_SPLAT_OPENGL_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer::GaussianSplat::OpenGL {

bool render(const Ecs::World& world, Internal::FrameOutput& output);
void shutdown();

} // namespace Renderer::GaussianSplat::OpenGL

#endif
