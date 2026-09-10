#ifndef HORSE_RENDERER_SHADING_STATE_HPP
#define HORSE_RENDERER_SHADING_STATE_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Environment.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

namespace Renderer::Internal {

struct ShadingState {
    EnvironmentState environment{};
    Renderer::Scenes::LightState light{};
};

void updateShadingState(const Ecs::World& world);
const ShadingState& shadingState();

} // namespace Renderer::Internal

#endif
