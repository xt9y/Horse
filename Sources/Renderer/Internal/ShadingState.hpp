#ifndef HORSE_RENDERER_INTERNAL_SHADING_STATE_HPP
#define HORSE_RENDERER_INTERNAL_SHADING_STATE_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Environment.hpp"
#include "Renderer/Lighting/Lighting.hpp"

namespace Renderer::Internal {

struct ShadingState {
    EnvironmentState environment{};
    Lighting::State lighting{};
};

void updateShadingState(const Ecs::World& world);
const ShadingState& shadingState();

} // namespace Renderer::Internal

#endif
