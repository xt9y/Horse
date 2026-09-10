#ifndef RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_TRACE_SCENE_HPP
#define RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_TRACE_SCENE_HPP

#include "Renderer/Scenes/SceneCache.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::GlobalIllumination {

struct TraceHit {
    bool found = false;
    float distance = 0.0f;
    Vec3 position{};
    Vec3 normal {0.0f, 1.0f, 0.0f};
    Vec2 uv{};
    std::uint32_t triangle = 0u;
    std::uint32_t material = 0u;
};

struct TraceBounds {
    Vec3 minimum{};
    Vec3 maximum{};
    bool valid = false;
};

class TraceScene {
public:
    bool build(
        const Ecs::World& world,
        const std::vector<Scenes::Scene::RenderItem>& items,
        std::string *error = nullptr
    );

    std::uint64_t signature(
        const Ecs::World& world,
        const std::vector<Scenes::Scene::RenderItem>& items
    ) const;

    TraceHit traceClosest(
        Vec3 origin,
        Vec3 direction,
        float maximum_distance,
        float ray_epsilon
    ) const;
    bool occluded(
        Vec3 origin,
        Vec3 direction,
        float maximum_distance,
        float ray_epsilon
    ) const;
    Vec3 albedo(const TraceHit& hit) const;
    TraceBounds bounds() const;
    bool empty() const { return cache_.triangles().empty(); }
    void clear() { cache_.clear(); }

    const Scenes::SceneCache& cache() const { return cache_; }

private:
    Scenes::SceneCache cache_;
};

} // namespace Renderer::GlobalIllumination

#endif
