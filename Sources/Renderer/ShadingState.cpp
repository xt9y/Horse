#include "Renderer/ShadingState.hpp"

#include "Renderer/Scenes/Scene.hpp"

namespace Renderer::Internal {
namespace {

ShadingState& storage()
{
    static thread_local ShadingState state;
    return state;
}

} // namespace

void updateShadingState(const Ecs::World& world)
{
    ShadingState& state = storage();
    state.environment = environmentState(world);
    state.light = Renderer::Scenes::lightState(Renderer::Scenes::Scene::lightState(world));
}

const ShadingState& shadingState()
{
    return storage();
}

} // namespace Renderer::Internal
