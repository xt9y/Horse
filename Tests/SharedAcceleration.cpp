#include <Ecs/Ecs.hpp>
#include <Renderer/Internal/AccelerationState.hpp>
#include <Renderer/Scenes/SceneCache.hpp>

#include <cassert>
#include <string>

int main()
{
    Ecs::World world;
    Renderer::Internal::AccelerationState state;
    std::string error;

    assert(state.sync(world, &error));
    assert(error.empty());
    assert(state.synchronizations() == 1u);

    assert(state.sync(world, &error));
    assert(error.empty());
    assert(state.synchronizations() == 1u);

    const float original_cutoff = Renderer::Scenes::SceneCache::opacityCutoff();
    Renderer::Scenes::SceneCache::setOpacityCutoff(original_cutoff + 0.01f);
    assert(state.sync(world, &error));
    assert(error.empty());
    assert(state.synchronizations() == 2u);

    Renderer::Scenes::SceneCache::setOpacityCutoff(original_cutoff);
    return 0;
}
