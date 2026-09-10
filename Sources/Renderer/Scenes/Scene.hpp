#ifndef RW_ENGINE_RENDERER_SCENES_SCENE_HPP
#define RW_ENGINE_RENDERER_SCENES_SCENE_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"

#include <vector>

namespace Renderer::Scenes::Scene {

struct CameraState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    float fov_degrees = 60.0f;
    float near_plane = 0.1f;
    bool valid = false;
};

struct LightState {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Transform transform{};
    LightComponent light{};
    bool valid = false;
};

struct RenderItem {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    const Transform* transform = nullptr;
    const MeshComponent* mesh_component = nullptr;
    const Models::MeshData* mesh = nullptr;
    const Models::MaterialData* material = nullptr;
};

CameraState cameraState(const Ecs::World& world);
LightState lightState(const Ecs::World& world);
void collectRenderItems(const Ecs::World& world, std::vector<RenderItem>& out);

} // namespace Renderer::Scenes::Scene

#endif
