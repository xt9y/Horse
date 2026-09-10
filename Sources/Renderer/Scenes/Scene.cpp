#include "Renderer/Scenes/Scene.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Hierarchy.hpp"

#include <algorithm>

namespace Renderer::Scenes::Scene {
namespace {

bool usesAlphaTexture(const RenderItem& item)
{
    if (!item.material) return false;
    if (item.material->opacity_texture != Models::INVALID_TEXTURE) return true;
    if (item.material->diffuse_texture == Models::INVALID_TEXTURE) return false;
    const Models::TextureAsset* texture = Models::texture(item.material->diffuse_texture);
    return texture && texture->image.meaningful_alpha;
}

Transform resolvedTransform(const Ecs::World& world, Ecs::Entity entity, const Transform& local)
{
    Transform result{};
    return Hierarchy::worldTransform(world, entity, &result) ? result : local;
}

} // namespace

CameraState cameraState(const Ecs::World& world)
{
    CameraState out;
    const Ecs::Entity entity = Camera::activeCamera(world);
    if (entity == Ecs::INVALID_ENTITY) return out;

    const Transform* transform = world.get<Transform>(entity);
    const Camera::CameraComponent* camera = world.get<Camera::CameraComponent>(entity);
    if (!transform || !camera) return out;

    out.entity = entity;
    out.transform = resolvedTransform(world, entity, *transform);
    out.fov_degrees = camera->fov_degrees;
    out.near_plane = camera->near_plane;
    out.valid = true;
    return out;
}

LightState lightState(const Ecs::World& world)
{
    LightState out;
    for (const Ecs::Entity entity : world.entities()) {
        const LightComponent* light = world.get<LightComponent>(entity);
        const Transform* transform = world.get<Transform>(entity);
        if (!light || !transform) continue;

        out.entity = entity;
        out.transform = resolvedTransform(world, entity, *transform);
        out.light = *light;
        out.valid = true;
        break;
    }
    return out;
}

void collectRenderItems(const Ecs::World& world, std::vector<RenderItem>& out)
{
    out.clear();

    for (const Ecs::Entity entity : world.entities()) {
        const RenderableComponent* renderable = world.get<RenderableComponent>(entity);
        if (!renderable || !renderable->visible) continue;

        const MeshComponent* mesh_component = world.get<MeshComponent>(entity);
        const Transform* transform = world.get<Transform>(entity);
        if (!mesh_component || !transform) continue;

        const Models::MeshData* mesh = Models::mesh(mesh_component->mesh);
        if (!mesh) continue;

        out.push_back(RenderItem{
            .entity = entity,
            .transform = TransformState{resolvedTransform(world, entity, *transform), true},
            .mesh_component = mesh_component,
            .mesh = mesh,
            .material = Models::material(mesh_component->material),
        });
    }

    std::stable_partition(out.begin(), out.end(), usesAlphaTexture);
}

} // namespace Renderer::Scenes::Scene
