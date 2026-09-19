#include <Renderer/Scenes/SceneCache.hpp>

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using Renderer::Scenes::SceneCache;

    SceneCache cache;
    const SceneCache::Profile initial = cache.profile();
    assert(initial.resource_rebuild_ms == 0.0);
    assert(initial.geometry_rebuild_ms == 0.0);
    assert(initial.geometry_update_ms == 0.0);
    assert(initial.bvh_build_ms == 0.0);
    assert(initial.bvh_refit_ms == 0.0);

    Ecs::World world;
    std::vector<Renderer::Scenes::Scene::RenderItem> items;
    std::string error;
    assert(cache.sync(world, items, 16u, &error));
    assert(error.empty());
    assert(cache.resourceUpdates() == 1u);
    assert(cache.topologyUpdates() == 1u);
    assert(cache.geometryUpdates() == 1u);

    const SceneCache::Profile measured = cache.profile();
    assert(measured.resource_rebuild_ms >= 0.0);
    assert(measured.geometry_rebuild_ms >= 0.0);
    assert(measured.geometry_update_ms == 0.0);
    assert(measured.bvh_build_ms >= 0.0);
    assert(measured.bvh_refit_ms == 0.0);

    cache.clear();
    const SceneCache::Profile cleared = cache.profile();
    assert(cleared.resource_rebuild_ms == 0.0);
    assert(cleared.geometry_rebuild_ms == 0.0);
    assert(cleared.geometry_update_ms == 0.0);
    assert(cleared.bvh_build_ms == 0.0);
    assert(cleared.bvh_refit_ms == 0.0);

    return 0;
}
