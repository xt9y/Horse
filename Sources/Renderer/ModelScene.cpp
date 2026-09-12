#include "Renderer/ModelScene.hpp"

#include "Camera.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/ModelScenePointers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Renderer::ModelScene {
namespace {

constexpr float RadToDeg = 57.2957795130823208768f;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

Transform transformFrom(const Models::Mat4& matrix)
{
    Transform result;
    result.matrix_override = matrix;
    result.matrix_override_enabled = true;
    result.position = {matrix[12], matrix[13], matrix[14]};
    return result;
}

std::array<float, 16> instanceMatrix(Models::Vec3 translation, Models::Quat rotation, Models::Vec3 scale)
{
    const float length2 = rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w;
    if (length2 > 1.0e-20f) {
        const float inverse = 1.0f / std::sqrt(length2);
        rotation.x *= inverse;
        rotation.y *= inverse;
        rotation.z *= inverse;
        rotation.w *= inverse;
    } else {
        rotation = {};
    }

    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;
    return {
        (1.0f - 2.0f * (yy + zz)) * scale.x,
        (2.0f * (xy + wz)) * scale.x,
        (2.0f * (xz - wy)) * scale.x,
        0.0f,
        (2.0f * (xy - wz)) * scale.y,
        (1.0f - 2.0f * (xx + zz)) * scale.y,
        (2.0f * (yz + wx)) * scale.y,
        0.0f,
        (2.0f * (xz + wy)) * scale.z,
        (2.0f * (yz - wx)) * scale.z,
        (1.0f - 2.0f * (xx + yy)) * scale.z,
        0.0f,
        translation.x,
        translation.y,
        translation.z,
        1.0f,
    };
}

const Models::InstanceData *instancesForNode(Models::ModelHandle model, std::uint32_t node)
{
    for (std::size_t index = 0u; index < Models::instanceCount(model); ++index) {
        const Models::InstanceData *source = Models::instance(model, index);
        if (source && source->node == node) return source;
    }
    return nullptr;
}

std::vector<std::array<float, 16>> localInstanceMatrices(const Models::InstanceData& source)
{
    std::size_t count = std::max({source.translations.size(), source.rotations.size(), source.scales.size()});
    for (const auto& [semantic, attribute] : source.attributes) {
        (void)semantic;
        if (attribute.components != 0u)
            count = std::max(count, attribute.values.size() / attribute.components);
    }

    std::vector<std::array<float, 16>> result;
    result.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const Models::Vec3 translation = index < source.translations.size()
            ? source.translations[index] : Models::Vec3{};
        const Models::Quat rotation = index < source.rotations.size()
            ? source.rotations[index] : Models::Quat{};
        const Models::Vec3 scale = index < source.scales.size()
            ? source.scales[index] : Models::Vec3{1.0f, 1.0f, 1.0f};
        result.push_back(instanceMatrix(translation, rotation, scale));
    }
    return result;
}

bool dynamicPart(Models::ModelHandle model, std::size_t part_index)
{
    const Models::ModelPart *part = Models::part(model, part_index);
    if (!part) return false;
    const Models::MeshData *mesh = Models::mesh(part->mesh);
    if (!mesh) return false;
    if (!mesh->morph_targets.empty()) return true;
    const Models::NodeData *node = part->node != Models::INVALID_INDEX
        ? Models::node(model, part->node) : nullptr;
    return node && node->skin != Models::INVALID_INDEX;
}

bool addPart(
    Ecs::World& world,
    Models::ModelHandle model,
    std::size_t part_index,
    std::uint32_t variant,
    Ecs::Entity parent,
    bool visible,
    const Models::InstanceData *instances,
    PartBinding *binding,
    std::string *error)
{
    if (!binding) return false;
    const Models::ModelPart *source = Models::part(model, part_index);
    if (!source) return fail(error, "model scene references invalid part");
    const Models::MeshData *mesh = Models::mesh(source->mesh);
    if (!mesh) return fail(error, "model scene part references invalid mesh");

    const bool is_dynamic = dynamicPart(model, part_index);
    const Models::MeshHandle mesh_handle = is_dynamic ? Models::registerMesh(*mesh) : source->mesh;
    if (mesh_handle == Models::INVALID_MESH) return fail(error, "failed to allocate model scene mesh");

    const Ecs::Entity entity = world.createEntity();
    world.add<Transform>(entity, Transform{});
    if (parent != Ecs::INVALID_ENTITY) world.add<Parent>(entity, Parent{parent});
    world.add<MeshComponent>(entity, MeshComponent{
        mesh_handle,
        Models::Runtime::materialForVariant(model, part_index, variant),
    });
    world.add<RenderableComponent>(entity, RenderableComponent{visible});
    if (instances) world.add<InstanceComponent>(entity, InstanceComponent{localInstanceMatrices(*instances)});

    binding->part = static_cast<std::uint32_t>(part_index);
    binding->entity = entity;
    binding->mesh = mesh_handle;
    binding->dynamic_mesh = is_dynamic;
    return true;
}

void markSceneNodes(
    Models::ModelHandle model,
    std::uint32_t root,
    std::vector<std::uint8_t> *included)
{
    if (!included || root >= included->size()) return;
    std::vector<std::uint32_t> stack{root};
    while (!stack.empty()) {
        const std::uint32_t current = stack.back();
        stack.pop_back();
        if (current >= included->size() || (*included)[current] != 0u) continue;
        (*included)[current] = 1u;
        const Models::NodeData *node = Models::node(model, current);
        if (!node) continue;
        for (std::uint32_t child : node->children) stack.push_back(child);
    }
}

LightType lightType(Models::AssetLightType type)
{
    switch (type) {
        case Models::AssetLightType::Directional: return LightType::Directional;
        case Models::AssetLightType::Spot: return LightType::Spot;
        case Models::AssetLightType::Point:
        default: return LightType::Point;
    }
}

bool updatePartMesh(
    Models::ModelHandle model,
    const Models::Runtime::Pose& pose,
    const PartBinding& binding,
    std::string *error)
{
    if (!binding.dynamic_mesh) return true;
    const Models::ModelPart *part = Models::part(model, binding.part);
    if (!part) return fail(error, "model scene dynamic part is invalid");
    const Models::MeshData *source = Models::mesh(part->mesh);
    if (!source) return fail(error, "model scene dynamic source mesh is invalid");

    Models::Runtime::DeformedPart deformed;
    if (!Models::Runtime::deformPart(model, binding.part, pose, &deformed, error)) return false;
    Models::MeshData replacement = *source;
    replacement.vertices = std::move(deformed.vertices);
    replacement.bounds = deformed.bounds;
    if (!Models::updateMesh(binding.mesh, replacement))
        return fail(error, "failed to update model scene dynamic mesh");
    return true;
}

} // namespace

bool instantiate(
    Ecs::World& world,
    Models::ModelHandle model,
    Instance *output,
    const Options& options,
    std::string *error)
{
    if (error) error->clear();
    if (!output || model == Models::INVALID_MODEL) return fail(error, "invalid model scene target");
    if (Models::nodeCount(model) == 0u && Models::partCount(model) == 0u)
        return fail(error, "model has no scene nodes or render parts");
    if (output->model != Models::INVALID_MODEL) destroy(world, *output);

    Models::Runtime::Pose pose;
    if (!Models::Runtime::reset(model, &pose, error)) return false;

    std::uint32_t scene_index = options.scene;
    if (scene_index == Models::INVALID_INDEX) scene_index = Models::defaultScene(model);
    if (scene_index == Models::INVALID_INDEX && Models::sceneCount(model) != 0u) scene_index = 0u;
    if (scene_index != Models::INVALID_INDEX && scene_index >= Models::sceneCount(model))
        return fail(error, "model scene index is invalid");
    if (options.variant != Models::INVALID_INDEX && options.variant >= Models::variantCount(model))
        return fail(error, "model material variant is invalid");

    std::vector<std::uint8_t> included(Models::nodeCount(model), scene_index == Models::INVALID_INDEX ? 1u : 0u);
    if (scene_index != Models::INVALID_INDEX) {
        const Models::SceneData *scene = Models::scene(model, scene_index);
        if (!scene) return fail(error, "model scene is unavailable");
        for (std::uint32_t root : scene->nodes) markSceneNodes(model, root, &included);
    }

    Instance result;
    result.model = model;
    result.scene = scene_index;
    result.variant = options.variant;
    result.nodes.resize(Models::nodeCount(model));

    bool activated_camera = false;
    for (std::size_t node_index = 0u; node_index < Models::nodeCount(model); ++node_index) {
        NodeBinding& binding = result.nodes[node_index];
        binding.node = static_cast<std::uint32_t>(node_index);
        if (included[node_index] == 0u) continue;
        const Models::NodeData *source = Models::node(model, node_index);
        if (!source || node_index >= pose.nodes.size()) {
            destroy(world, result);
            return fail(error, "model scene node is unavailable");
        }

        binding.entity = world.createEntity();
        world.add<Transform>(binding.entity, transformFrom(pose.nodes[node_index].local));
        world.add<InteractionComponent>(binding.entity, InteractionComponent{source->selectable, source->hoverable});

        if (options.instantiate_cameras && source->camera != Models::INVALID_INDEX) {
            const Models::CameraData *camera = Models::camera(model, source->camera);
            if (!camera) {
                destroy(world, result);
                return fail(error, "model scene camera is invalid");
            }
            Camera::CameraComponent component;
            component.fov_degrees = camera->yfov * RadToDeg;
            component.near_plane = camera->znear;
            component.active = options.activate_first_camera && !activated_camera;
            component.projection = camera->type == Models::CameraType::Orthographic
                ? Camera::Projection::Orthographic : Camera::Projection::Perspective;
            component.far_plane = camera->zfar;
            component.aspect_ratio = camera->aspect_ratio;
            component.xmag = camera->xmag;
            component.ymag = camera->ymag;
            world.add<Camera::CameraComponent>(binding.entity, component);
            activated_camera = activated_camera || component.active;
        }

        if (options.instantiate_lights && source->light != Models::INVALID_INDEX) {
            const Models::LightData *light = Models::light(model, source->light);
            if (!light) {
                destroy(world, result);
                return fail(error, "model scene light is invalid");
            }
            world.add<LightComponent>(binding.entity, LightComponent{
                .type = lightType(light->type),
                .color = {light->color.x, light->color.y, light->color.z},
                .intensity = light->intensity,
                .range = light->range,
                .inner_cone_degrees = light->inner_cone_angle * RadToDeg,
                .outer_cone_degrees = light->outer_cone_angle * RadToDeg,
            });
        }
    }

    for (std::size_t node_index = 0u; node_index < Models::nodeCount(model); ++node_index) {
        if (included[node_index] == 0u) continue;
        NodeBinding& binding = result.nodes[node_index];
        const Models::NodeData *source = Models::node(model, node_index);
        if (!source || binding.entity == Ecs::INVALID_ENTITY) continue;
        if (source->parent >= 0 && static_cast<std::size_t>(source->parent) < result.nodes.size()) {
            const Ecs::Entity parent = result.nodes[static_cast<std::size_t>(source->parent)].entity;
            if (parent != Ecs::INVALID_ENTITY) world.add<Parent>(binding.entity, Parent{parent});
        }

        const Models::InstanceData *instances = instancesForNode(model, static_cast<std::uint32_t>(node_index));
        binding.parts.reserve(source->parts.size());
        for (std::uint32_t part_index : source->parts) {
            PartBinding part_binding;
            if (!addPart(
                    world,
                    model,
                    part_index,
                    options.variant,
                    binding.entity,
                    pose.nodes[node_index].visible,
                    instances,
                    &part_binding,
                    error)) {
                destroy(world, result);
                return false;
            }
            binding.parts.push_back(part_binding);
        }
    }

    if (scene_index == Models::INVALID_INDEX) {
        for (std::size_t part_index = 0u; part_index < Models::partCount(model); ++part_index) {
            const Models::ModelPart *part = Models::part(model, part_index);
            if (!part || part->node != Models::INVALID_INDEX) continue;
            PartBinding binding;
            if (!addPart(world, model, part_index, options.variant, Ecs::INVALID_ENTITY, true, nullptr, &binding, error)) {
                destroy(world, result);
                return false;
            }
            result.loose_parts.push_back(binding);
        }
    }

    *output = std::move(result);
    world.markChanged(Ecs::ChangeKind::Structure);
    world.markChanged(Ecs::ChangeKind::Resource);
    return true;
}

bool applyPose(
    Ecs::World& world,
    Instance& instance,
    const Models::Runtime::Pose& pose,
    std::string *error)
{
    if (error) error->clear();
    if (instance.model == Models::INVALID_MODEL || pose.model != instance.model)
        return fail(error, "pose does not belong to model scene instance");
    if (pose.nodes.size() < instance.nodes.size()) return fail(error, "model pose node count is too small");

    bool resources_changed = false;
    for (NodeBinding& binding : instance.nodes) {
        if (binding.entity == Ecs::INVALID_ENTITY || binding.node >= pose.nodes.size()) continue;
        Transform *transform = world.get<Transform>(binding.entity);
        if (!transform) return fail(error, "model scene node lost its transform component");
        *transform = transformFrom(pose.nodes[binding.node].local);
        if (InteractionComponent *interaction = world.get<InteractionComponent>(binding.entity)) {
            interaction->selectable = pose.nodes[binding.node].selectable;
            interaction->hoverable = pose.nodes[binding.node].hoverable;
        }
        for (PartBinding& part : binding.parts) {
            if (RenderableComponent *renderable = world.get<RenderableComponent>(part.entity))
                renderable->visible = pose.nodes[binding.node].visible;
            if (!updatePartMesh(instance.model, pose, part, error)) return false;
            resources_changed = resources_changed || part.dynamic_mesh;
        }
    }

    for (PartBinding& part : instance.loose_parts) {
        if (!updatePartMesh(instance.model, pose, part, error)) return false;
        resources_changed = resources_changed || part.dynamic_mesh;
    }

    if (!Pointers::apply(world, instance, pose, error)) return false;

    world.markChanged(Ecs::ChangeKind::Transform);
    world.markChanged(Ecs::ChangeKind::Animation);
    if (resources_changed) world.markChanged(Ecs::ChangeKind::Resource);
    return true;
}

bool setVariant(
    Ecs::World& world,
    Instance& instance,
    std::uint32_t variant,
    std::string *error)
{
    if (error) error->clear();
    if (instance.model == Models::INVALID_MODEL) return fail(error, "invalid model scene instance");
    if (variant != Models::INVALID_INDEX && variant >= Models::variantCount(instance.model))
        return fail(error, "model material variant is invalid");

    const auto update = [&](PartBinding& binding) -> bool {
        MeshComponent *mesh = world.get<MeshComponent>(binding.entity);
        if (!mesh) return false;
        mesh->material = Models::Runtime::materialForVariant(instance.model, binding.part, variant);
        return mesh->material != Models::INVALID_MATERIAL;
    };
    for (NodeBinding& node : instance.nodes)
        for (PartBinding& part : node.parts)
            if (!update(part)) return fail(error, "failed to apply model material variant");
    for (PartBinding& part : instance.loose_parts)
        if (!update(part)) return fail(error, "failed to apply model material variant");

    instance.variant = variant;
    world.markChanged(Ecs::ChangeKind::Resource);
    return true;
}

void destroy(Ecs::World& world, Instance& instance)
{
    for (PartBinding& part : instance.loose_parts)
        if (part.entity != Ecs::INVALID_ENTITY && world.alive(part.entity)) world.destroyEntity(part.entity);
    for (NodeBinding& node : instance.nodes) {
        for (PartBinding& part : node.parts)
            if (part.entity != Ecs::INVALID_ENTITY && world.alive(part.entity)) world.destroyEntity(part.entity);
    }
    for (NodeBinding& node : instance.nodes)
        if (node.entity != Ecs::INVALID_ENTITY && world.alive(node.entity)) world.destroyEntity(node.entity);
    instance = {};
}

} // namespace Renderer::ModelScene
