#ifndef RW_ENGINE_RENDERER_SCENES_SCENE_HPP
#define RW_ENGINE_RENDERER_SCENES_SCENE_HPP

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
};

struct LightState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    LightComponent light{};
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
    TransformState transform{};
    MeshState mesh_component{};
    const Models::MeshData* mesh = nullptr;
    const Models::MaterialData* material = nullptr;
};

CameraState cameraState(const Ecs::World& world);
LightState lightState(const Ecs::World& world);
void collectRenderItems(const Ecs::World& world, std::vector<RenderItem>& out);

} // namespace Renderer::Scenes::Scene

#endif
