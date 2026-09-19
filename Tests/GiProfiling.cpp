#include <Renderer/GlobalIllumination/Debug.hpp>

#include <cassert>

int main()
{
    using Renderer::GlobalIllumination::Debug::SceneUpdate;
    using Renderer::GlobalIllumination::Debug::Statistics;

    Statistics stats;
    assert(stats.scene_update == SceneUpdate::None);
    assert(stats.topology_updates == 0u);
    assert(stats.geometry_updates == 0u);
    assert(stats.resource_updates == 0u);
    assert(stats.scene_build_ms == 0.0);
    assert(stats.photon_build_ms == 0.0);
    assert(stats.probe_update_ms == 0.0);

    return 0;
}
