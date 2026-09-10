#ifndef HORSE_RENDERER_VISIBILITY_VISIBILITY_HPP
#define HORSE_RENDERER_VISIBILITY_VISIBILITY_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Renderer::Visibility {

enum class Classification {
    Outside,
    Intersecting,
    Inside,
};

struct Frustum {
    Vec3 position{};
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    float near_distance = 0.0f;
    float far_distance = 0.0f;
    float half_vertical_fov_radians = 0.0f;
    float aspect = 0.0f;
    bool valid = false;
};

struct Result {
    Frustum frustum{};
    std::vector<Ecs::Entity> visible;
    std::vector<Ecs::Entity> culled;
    std::size_t visible_triangles = 0u;
    std::size_t culled_triangles = 0u;
};

class System {
public:
    void setFarDistance(float value) { far_distance_ = value; }
    float farDistance() const { return far_distance_; }

    void setOverride(Result result);
    void clearOverride();
    bool hasOverride() const { return override_active_; }

    Frustum makeFrustum(const Ecs::World& world, int width, int height) const;
    Classification classify(const Frustum& frustum, const Scenes::Scene::RenderItem& item) const;
    Result evaluate(const Ecs::World& world, int width, int height) const;
    Result collectVisibleRenderItems(
        const Ecs::World& world,
        int width,
        int height,
        std::vector<Scenes::Scene::RenderItem>& out) const;
    Result buildEntityMask(
        const Ecs::World& world,
        int width,
        int height,
        std::vector<std::uint32_t>& out) const;
    std::array<Vec3, 8> corners(const Frustum& frustum) const;

private:
    float far_distance_ = 0.0f;
    bool override_active_ = false;
    Result override_result_{};
    std::vector<std::uint8_t> override_visible_;
};

System& system();

} // namespace Renderer::Visibility

#endif
