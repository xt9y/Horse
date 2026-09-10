#include "Renderer/Scenes/Scene.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Hierarchy.hpp"
#include "Renderer/Lod.hpp"

#include <algorithm>
#include <cmath>

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

float distanceSquared(Vec3 a, Vec3 b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z;
}

MeshComponent selectedMesh(
    const Ecs::World& world,
    Ecs::Entity entity,
    const MeshComponent& base,
    Vec3 world_position,
    const CameraState& camera)
{
    MeshComponent selected = base;
    const LodGroup *group = world.get<LodGroup>(entity);
    if (!group || group->levels.empty() || !camera.valid) return selected;

    const float distance = std::sqrt(distanceSquared(world_position, camera.transform.position));
    float selected_threshold = -1.0f;
    for (const LodLevel& level : group->levels) {
        const float threshold = std::max(level.minimum_distance, 0.0f);
        if (level.mesh == UINT32_MAX || distance < threshold || threshold < selected_threshold) continue;
        selected.mesh = level.mesh;
        if (level.material != UINT32_MAX) selected.material = level.material;
        selected_threshold = threshold;
    }
    return selected;
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
    const CameraState camera = cameraState(world);

    for (const Ecs::Entity entity : world.entities()) {
        const RenderableComponent* renderable = world.get<RenderableComponent>(entity);
        if (!renderable || !renderable->visible) continue;

        const MeshComponent* mesh_component = world.get<MeshComponent>(entity);
        const Transform* transform = world.get<Transform>(entity);
        if (!mesh_component || !transform) continue;

        const Transform world_transform = resolvedTransform(world, entity, *transform);
        const MeshComponent selected = selectedMesh(
            world,
            entity,
            *mesh_component,
            world_transform.position,
            camera
        );
        const Models::MeshData* mesh = Models::mesh(selected.mesh);
        if (!mesh) continue;

        out.push_back(RenderItem{
            .entity = entity,
            .transform = TransformState{world_transform, true},
            .mesh_component = MeshState{selected, true},
            .mesh = mesh,
            .material = Models::material(selected.material),
        });
    }

    std::stable_partition(out.begin(), out.end(), usesAlphaTexture);
}

} // namespace Renderer::Scenes::Scene
