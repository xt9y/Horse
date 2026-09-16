#include "Renderer/Internal/ShadingState.hpp"

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
    state.lighting = Lighting::state(world);
}

const ShadingState& shadingState()
{
    return storage();
}

} // namespace Renderer::Internal
