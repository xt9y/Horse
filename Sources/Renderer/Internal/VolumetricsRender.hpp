#ifndef HORSE_RENDERER_INTERNAL_VOLUMETRICS_RENDER_HPP
#define HORSE_RENDERER_INTERNAL_VOLUMETRICS_RENDER_HPP

#include "Ecs/Ecs.hpp"

namespace Renderer::Internal {
struct FrameOutput;
}

namespace Renderer::Volumetrics {

bool render(const Ecs::World& world, Internal::FrameOutput& output);

} // namespace Renderer::Volumetrics

#endif
