#ifndef HORSE_RENDERER_INTERNAL_DISPLAY_HPP
#define HORSE_RENDERER_INTERNAL_DISPLAY_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer::Internal {

bool renderDisplay(const Ecs::World& world, FrameOutput& output);

} // namespace Renderer::Internal

#endif
