#ifndef HORSE_RENDERER_INTERNAL_LINES_RENDER_PASS_HPP
#define HORSE_RENDERER_INTERNAL_LINES_RENDER_PASS_HPP

#include "Ecs/Ecs.hpp"

namespace Renderer::Internal {
struct FrameOutput;
}

namespace Renderer::Lines::Internal {

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output);

} // namespace Renderer::Lines::Internal

#endif
