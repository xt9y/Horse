#include "Renderer/Scenes/SceneCache.hpp"

namespace Renderer::Scenes {

bool SceneCache::syncGeometry(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::string *error)
{
    if (maximum_triangles_ == 0u) {
        if (error) *error = "SceneCache maximum triangle count was not configured";
        return false;
    }
    if (error) error->clear();

    const std::uint64_t current_topology_signature = topologySignature(items);
    const std::uint64_t current_geometry_signature = signature(world, items);
    if (!topology_initialized_ || current_topology_signature != topology_signature_) {
        if (!rebuildGeometry(world, items, error)) {
            clearGeometry();
            return false;
        }
        topology_signature_ = current_topology_signature;
        topology_initialized_ = true;
        ++topology_revision_;
        ++topology_updates_;
        geometry_signature_ = current_geometry_signature;
        geometry_initialized_ = true;
        ++geometry_revision_;
        ++geometry_updates_;
    } else if (!geometry_initialized_ || current_geometry_signature != geometry_signature_) {
        if (!updateGeometry(world, items, error)) {
            clearGeometry();
            return false;
        }
        geometry_signature_ = current_geometry_signature;
        geometry_initialized_ = true;
        ++geometry_revision_;
        ++geometry_updates_;
    }

    return true;
}

} // namespace Renderer::Scenes
