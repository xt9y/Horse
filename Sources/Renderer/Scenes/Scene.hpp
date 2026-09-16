#ifndef HORSE_RENDERER_SCENES_SCENE_HPP
#define HORSE_RENDERER_SCENES_SCENE_HPP

#include "Camera/Camera.hpp"
#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"

#include <cstdint>
#include <vector>

namespace Renderer::Scenes::Scene {

struct CameraState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    float fov_degrees = 0.0f;
    float near_plane = 0.0f;
    bool valid = false;
    Camera::Projection projection = Camera::Projection::Perspective;
    float far_plane = 0.0f;
    float aspect_ratio = 0.0f;
    float xmag = 1.0f;
    float ymag = 1.0f;
};

struct LightState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    LightComponent light{};
    ShadowComponent shadow{};
    VolumetricLightComponent volumetric{};
    bool has_shadow = false;
    bool has_volumetric = false;
    bool valid = false;
};

struct TransformState {
    Transform value{};
    bool valid = false;

    explicit operator bool() const { return valid; }
    const Transform& operator*() const { return value; }
    const Transform* operator->() const { return valid ? &value : nullptr; }
};

struct MeshState {
    MeshComponent value{};
    bool valid = false;

    explicit operator bool() const { return valid; }
    const MeshComponent& operator*() const { return value; }
    const MeshComponent* operator->() const { return valid ? &value : nullptr; }
};

struct RenderItem {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    std::uint32_t instance_index = UINT32_MAX;
    RenderLayer layer = RenderLayer::World;
    TransformState transform{};
    MeshState mesh_component{};
    const Models::MeshData* mesh = nullptr;
    const Models::MaterialData* material = nullptr;
};

struct RenderRevision {
    std::uint64_t structure = 0u;
    std::uint64_t transform = 0u;
    std::uint64_t resource = 0u;
    std::uint64_t animation = 0u;
    std::uint64_t camera = 0u;
    std::uint64_t model_resource = 0u;

    bool operator==(const RenderRevision&) const = default;
};

CameraState cameraState(const Ecs::World& world);
void collectLights(const Ecs::World& world, std::vector<LightState>& out);
RenderRevision renderRevision(const Ecs::World& world);
void collectRenderItems(const Ecs::World& world, std::vector<RenderItem>& out);
void collectGaussianItems(const Ecs::World& world, std::vector<RenderItem>& out);

} // namespace Renderer::Scenes::Scene

#endif
