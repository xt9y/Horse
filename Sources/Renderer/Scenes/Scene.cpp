#include "Renderer/Scenes/Scene.hpp"

#include "Camera/Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/GaussianSplat.hpp"
#include "Models/Internal/ResourcePump.hpp"
#include "Renderer/Hierarchy.hpp"
#include "Renderer/Internal/GeometryComponents.hpp"
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

RenderLayer resolvedRenderLayer(const Ecs::World& world, Ecs::Entity entity)
{
    Ecs::Entity current = entity;
    const std::size_t maximum = world.size() + 1u;
    for (std::size_t depth = 0u; depth < maximum; ++depth) {
        if (const RenderLayerComponent *layer = world.get<RenderLayerComponent>(current))
            return layer->layer;
        const Parent *parent = world.get<Parent>(current);
        if (!parent || parent->entity == Ecs::INVALID_ENTITY || !world.alive(parent->entity)) break;
        current = parent->entity;
    }
    return RenderLayer::World;
}

float distanceSquared(Vec3 a, Vec3 b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return x * x + y * y + z * z;
}

Internal::MeshComponent selectedMesh(
    const Ecs::World& world,
    Ecs::Entity entity,
    const Internal::MeshComponent& base,
    Vec3 world_position,
    const CameraState& camera)
{
    Internal::MeshComponent selected = base;
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
    const Internal::MeshComponent& base,
    const Transform& transform,
    const CameraState& camera,
    bool gaussian,
    std::vector<RenderItem>& out)
{
    const Internal::MeshComponent selected = selectedMesh(world, entity, base, transform.position, camera);
    const Models::MeshData* mesh = Models::mesh(selected.mesh);
    if (!mesh || Models::GaussianSplat::isGaussianSplat(*mesh) != gaussian) return;

    out.push_back(RenderItem{
        .entity = entity,
        .instance_index = instance_index,
        .layer = resolvedRenderLayer(world, entity),
        .transform = TransformState{transform, true},
        .mesh_component = MeshBinding{
            .mesh = selected.mesh,
            .material = selected.material,
            .valid = true,
        },
        .mesh = mesh,
        .material = Models::material(selected.material),
    });
}

void collectItems(const Ecs::World& world, bool gaussian, std::vector<RenderItem>& out)
{
    Models::Internal::pumpResources();
    out.clear();
    const CameraState camera = cameraState(world);

    for (const Ecs::Entity entity : world.entities()) {
        const Internal::RenderableComponent* renderable = world.get<Internal::RenderableComponent>(entity);
        if (!renderable || !renderable->visible) continue;

        const Internal::MeshComponent* mesh_component = world.get<Internal::MeshComponent>(entity);
        const Transform* transform = world.get<Transform>(entity);
        if (!mesh_component || !transform) continue;

        const Transform world_transform = resolvedTransform(world, entity, *transform);
        const Internal::InstanceComponent* instances = world.get<Internal::InstanceComponent>(entity);
        if (!instances) {
            appendItem(world, entity, UINT32_MAX, *mesh_component, world_transform, camera, gaussian, out);
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
                gaussian,
                out
            );
        }
    }
}

bool refreshItemTransform(const Ecs::World& world, RenderItem& item)
{
    if (item.entity == Ecs::INVALID_ENTITY || !world.alive(item.entity)) return false;
    if (world.has<LodGroup>(item.entity)) return false;

    const Internal::RenderableComponent* renderable =
        world.get<Internal::RenderableComponent>(item.entity);
    const Internal::MeshComponent* mesh_component =
        world.get<Internal::MeshComponent>(item.entity);
    const Transform* transform = world.get<Transform>(item.entity);
    if (!renderable || !renderable->visible || !mesh_component || !transform) return false;
    if (!item.mesh_component ||
        mesh_component->mesh != item.mesh_component->mesh ||
        mesh_component->material != item.mesh_component->material)
        return false;

    const Transform world_transform = resolvedTransform(world, item.entity, *transform);
    const Internal::InstanceComponent* instances =
        world.get<Internal::InstanceComponent>(item.entity);
    if (!instances) {
        if (item.instance_index != UINT32_MAX) return false;
        item.transform = TransformState{world_transform, true};
        return true;
    }

    if (item.instance_index == UINT32_MAX || item.instance_index >= instances->matrices.size())
        return false;

    Transform instanced{};
    instanced.matrix_override = Math::multiply(
        Math::modelMatrix(world_transform),
        instances->matrices[item.instance_index]
    );
    instanced.matrix_override_enabled = true;
    instanced.position = Math::transformPoint(
        instanced.matrix_override,
        {0.0f, 0.0f, 0.0f}
    );
    item.transform = TransformState{instanced, true};
    return true;
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

void collectLights(const Ecs::World& world, std::vector<LightState>& out)
{
    out.clear();
    for (const Ecs::Entity entity : world.entities()) {
        const LightComponent* light = world.get<LightComponent>(entity);
        const Transform* transform = world.get<Transform>(entity);
        if (!light || !transform) continue;

        LightState state;
        state.entity = entity;
        state.transform = resolvedTransform(world, entity, *transform);
        state.light = *light;
        if (const ShadowComponent* shadow = world.get<ShadowComponent>(entity)) {
            state.shadow = *shadow;
            state.has_shadow = true;
        }
        if (const VolumetricLightComponent* volumetric = world.get<VolumetricLightComponent>(entity)) {
            state.volumetric = *volumetric;
            state.has_volumetric = true;
        }
        state.valid = true;
        out.push_back(state);
    }
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
    collectItems(world, false, out);
    std::stable_partition(
        out.begin(),
        out.end(),
        [](const RenderItem& item) { return !usesAlpha(item); }
    );
}

bool refreshRenderItemTransforms(const Ecs::World& world, std::vector<RenderItem>& items)
{
    for (RenderItem& item : items) {
        if (!refreshItemTransform(world, item)) return false;
    }
    return true;
}

bool refreshRenderItemTransforms(
    const Ecs::World& world,
    std::vector<RenderItem>& items,
    const std::vector<std::size_t>& changed_items)
{
    for (const std::size_t item_index : changed_items) {
        if (item_index >= items.size() || !refreshItemTransform(world, items[item_index]))
            return false;
    }
    return true;
}

void collectGaussianItems(const Ecs::World& world, std::vector<RenderItem>& out)
{
    collectItems(world, true, out);
}

} // namespace Renderer::Scenes::Scene
