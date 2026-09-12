#include "Renderer/Scenes/Scene.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Hierarchy.hpp"
#include "Renderer/Lod.hpp"
#include "Renderer/Math.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::Scenes::Scene {
namespace {

bool usesAlpha(const RenderItem& item)
{
    if (!item.material) return false;
    if (item.material->alpha_mode == Models::AlphaMode::Blend || item.material->opacity < 0.999f)
        return true;
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

void appendItem(
    const Ecs::World& world,
    Ecs::Entity entity,
    std::uint32_t instance_index,
    const MeshComponent& base,
    const Transform& transform,
    const CameraState& camera,
    std::vector<RenderItem>& out)
{
    const MeshComponent selected = selectedMesh(world, entity, base, transform.position, camera);
    const Models::MeshData* mesh = Models::mesh(selected.mesh);
    if (!mesh) return;

    out.push_back(RenderItem{
        .entity = entity,
        .instance_index = instance_index,
        .transform = TransformState{transform, true},
        .mesh_component = MeshState{selected, true},
        .mesh = mesh,
        .material = Models::material(selected.material),
    });
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
    out.projection = camera->projection;
    out.far_plane = camera->far_plane;
    out.aspect_ratio = camera->aspect_ratio;
    out.xmag = camera->xmag;
    out.ymag = camera->ymag;
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

RenderRevision renderRevision(const Ecs::World& world)
{
    bool camera_dependent_lod = false;
    world.each<LodGroup>([&](Ecs::Entity, const LodGroup&) {
        camera_dependent_lod = true;
    });

    return {
        world.changeRevision(Ecs::ChangeKind::Structure),
        world.changeRevision(Ecs::ChangeKind::Transform),
        world.changeRevision(Ecs::ChangeKind::Resource),
        world.changeRevision(Ecs::ChangeKind::Animation),
        camera_dependent_lod ? world.changeRevision(Ecs::ChangeKind::Camera) : 0u,
        Models::resourceRevision(),
    };
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
        const InstanceComponent* instances = world.get<InstanceComponent>(entity);
        if (!instances) {
            appendItem(world, entity, UINT32_MAX, *mesh_component, world_transform, camera, out);
            continue;
        }

        const Math::Mat4 base = Math::modelMatrix(world_transform);
        for (std::size_t index = 0u; index < instances->matrices.size(); ++index) {
            Transform instanced{};
            instanced.matrix_override = Math::multiply(base, instances->matrices[index]);
            instanced.matrix_override_enabled = true;
            instanced.position = Math::transformPoint(instanced.matrix_override, {0.0f, 0.0f, 0.0f});
            appendItem(
                world,
                entity,
                static_cast<std::uint32_t>(index),
                *mesh_component,
                instanced,
                camera,
                out
            );
        }
    }

    std::stable_partition(
        out.begin(),
        out.end(),
        [](const RenderItem& item) { return !usesAlpha(item); }
    );
}

} // namespace Renderer::Scenes::Scene
