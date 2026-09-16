#include "Renderer/ModelScene.hpp"

#include "Camera/Camera.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Internal/GeometryComponents.hpp"
#include "Renderer/Internal/ModelGeometry.hpp"
#include "Renderer/Internal/ModelScenePointers.hpp"
#include "Renderer/Math.hpp"

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

bool dynamicModel(Models::ModelHandle model)
{
    for (std::size_t part_index = 0u; part_index < Models::partCount(model); ++part_index)
        if (dynamicPart(model, part_index)) return true;
    return false;
}

bool addPart(
    Ecs::World& world,
    Models::ModelHandle model,
    std::size_t part_index,
    std::uint32_t variant,
    Ecs::Entity parent,
    Ecs::Entity pose_entity,
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
    const Models::MeshHandle mesh_handle = source->mesh;

    const Ecs::Entity entity = world.createEntity();
    world.add<Transform>(entity, Transform{});
    if (parent != Ecs::INVALID_ENTITY) world.add<Parent>(entity, Parent{parent});
    world.add<Internal::MeshComponent>(entity, Internal::MeshComponent{
        mesh_handle,
        Models::Runtime::materialForVariant(model, part_index, variant),
    });
    world.add<Internal::RenderableComponent>(entity, Internal::RenderableComponent{visible});
    if (instances)
        world.add<Internal::InstanceComponent>(entity, Internal::InstanceComponent{localInstanceMatrices(*instances)});
    if (is_dynamic) {
        if (pose_entity == Ecs::INVALID_ENTITY)
            return fail(error, "model scene dynamic part has no pose state");
        world.add<Internal::ModelDeformComponent>(entity, Internal::ModelDeformComponent{
            model,
            static_cast<std::uint32_t>(part_index),
            pose_entity,
        });
    }

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

bool align(
    Ecs::World& world,
    Instance& instance,
    const Models::Runtime::Pose& pose,
    std::uint32_t target_basis,
    const Models::Mat4& source_basis,
    std::string *error)
{
    if (target_basis >= pose.nodes.size()) return fail(error, "model animation target basis is invalid");

    Math::Mat4 inverse_basis {};
    if (!Math::inverseMatrix(pose.nodes[target_basis].world, &inverse_basis))
        return fail(error, "model animation target basis is not invertible");
    const Math::Mat4 correction = Math::multiply(source_basis, inverse_basis);

    for (NodeBinding& binding : instance.nodes) {
        if (binding.entity == Ecs::INVALID_ENTITY || binding.node >= pose.nodes.size()) continue;
        const Models::NodeData *source = Models::node(instance.model, binding.node);
        if (!source) return fail(error, "model animation target node is unavailable");

        bool root = source->parent < 0;
        if (!root) {
            const std::size_t parent = static_cast<std::size_t>(source->parent);
            root = parent >= instance.nodes.size() || instance.nodes[parent].entity == Ecs::INVALID_ENTITY;
        }
        if (!root) continue;

        Transform *transform = world.get<Transform>(binding.entity);
        if (!transform) return fail(error, "model animation target root lost its transform component");
        transform->matrix_override = Math::multiply(correction, pose.nodes[binding.node].local);
        transform->matrix_override_enabled = true;
        transform->position = {
            transform->matrix_override[12],
            transform->matrix_override[13],
            transform->matrix_override[14],
        };
    }

    world.markChanged(Ecs::ChangeKind::Transform);
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
    if (dynamicModel(model)) {
        result.pose_entity = world.createEntity();
        world.add<Internal::ModelPoseComponent>(result.pose_entity, Internal::ModelPoseComponent{pose, 1u});
    }

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
        bool parented = false;
        if (source->parent >= 0 && static_cast<std::size_t>(source->parent) < result.nodes.size()) {
            const Ecs::Entity parent = result.nodes[static_cast<std::size_t>(source->parent)].entity;
            if (parent != Ecs::INVALID_ENTITY) {
                world.add<Parent>(binding.entity, Parent{parent});
                parented = true;
            }
        }
        if (!parented && options.parent != Ecs::INVALID_ENTITY)
            world.add<Parent>(binding.entity, Parent{options.parent});

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
                    result.pose_entity,
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
            if (!addPart(
                    world,
                    model,
                    part_index,
                    options.variant,
                    options.parent,
                    result.pose_entity,
                    true,
                    nullptr,
                    &binding,
                    error)) {
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

    if (instance.pose_entity != Ecs::INVALID_ENTITY) {
        Internal::ModelPoseComponent *state = world.get<Internal::ModelPoseComponent>(instance.pose_entity);
        if (!state) return fail(error, "model scene lost its pose state");
        state->pose = pose;
        ++state->revision;
        if (state->revision == 0u) state->revision = 1u;
    }

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
            if (Internal::RenderableComponent *renderable =
                    world.get<Internal::RenderableComponent>(part.entity))
                renderable->visible = pose.nodes[binding.node].visible;
        }
    }

    if (!Pointers::apply(world, instance, pose, error)) return false;

    world.markChanged(Ecs::ChangeKind::Transform);
    world.markChanged(Ecs::ChangeKind::Animation);
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
        Internal::MeshComponent *mesh = world.get<Internal::MeshComponent>(binding.entity);
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

std::size_t bind(
    Animation& animation,
    Instance& instance,
    const Models::Runtime::RetargetOptions& options)
{
    if (instance.model == Models::INVALID_MODEL || animation.targets.size() >= Models::INVALID_INDEX)
        return Models::INVALID_INDEX;
    const std::size_t index = animation.targets.size();
    animation.targets.push_back(AnimationTarget{
        .instance = &instance,
        .options = options,
    });
    return index;
}

bool attach(
    Animation& animation,
    std::size_t target,
    std::string_view source_node,
    std::string_view target_node)
{
    if (target >= animation.targets.size() || source_node.empty() || target_node.empty()) return false;
    for (const AnimationAttachment& attachment : animation.attachments)
        if (attachment.target == target) return false;
    animation.attachments.push_back(AnimationAttachment{
        .target = target,
        .source_node = std::string(source_node),
        .target_node = std::string(target_node),
    });
    return true;
}

bool play(
    Animation& animation,
    Models::ModelHandle model,
    std::size_t clip,
    bool loop,
    std::string *error)
{
    if (error) error->clear();
    if (model == Models::INVALID_MODEL || !Models::modelAnimation(model, clip))
        return fail(error, "invalid model scene animation clip");

    std::vector<Models::Runtime::Retarget> bindings(animation.targets.size());
    for (std::size_t index = 0u; index < animation.targets.size(); ++index) {
        AnimationTarget& target = animation.targets[index];
        if (!target.instance || target.instance->model == Models::INVALID_MODEL)
            return fail(error, "model scene animation target is invalid");
        if (!Models::Runtime::bindRetarget(
                model,
                target.instance->model,
                &bindings[index],
                target.options,
                error)) return false;
    }

    std::vector<std::uint32_t> attachment_sources(animation.attachments.size(), Models::INVALID_INDEX);
    std::vector<std::uint32_t> attachment_targets(animation.attachments.size(), Models::INVALID_INDEX);
    for (std::size_t index = 0u; index < animation.attachments.size(); ++index) {
        const AnimationAttachment& attachment = animation.attachments[index];
        if (attachment.target >= animation.targets.size())
            return fail(error, "model scene animation attachment target is invalid");
        const AnimationTarget& target = animation.targets[attachment.target];
        if (!target.instance) return fail(error, "model scene animation attachment lost its target");

        const std::size_t source = Models::nodeIndex(model, attachment.source_node);
        const std::size_t target_basis = Models::nodeIndex(target.instance->model, attachment.target_node);
        if (source == Models::INVALID_INDEX)
            return fail(error, "model scene animation source attachment was not found: " + attachment.source_node);
        if (target_basis == Models::INVALID_INDEX)
            return fail(error, "model scene animation target attachment was not found: " + attachment.target_node);
        attachment_sources[index] = static_cast<std::uint32_t>(source);
        attachment_targets[index] = static_cast<std::uint32_t>(target_basis);
    }

    for (std::size_t index = 0u; index < animation.targets.size(); ++index)
        animation.targets[index].binding = std::move(bindings[index]);
    for (std::size_t index = 0u; index < animation.attachments.size(); ++index) {
        animation.attachments[index].source = attachment_sources[index];
        animation.attachments[index].target_basis = attachment_targets[index];
    }

    animation.model = model;
    animation.clip = static_cast<std::uint32_t>(clip);
    animation.time = 0.0f;
    animation.loop = loop;
    animation.active = true;
    return true;
}

bool update(
    Ecs::World& world,
    Animation& animation,
    float delta_seconds,
    std::string *error)
{
    if (error) error->clear();
    if (!animation.active) return true;

    const Models::ModelAnimationData *clip = Models::modelAnimation(animation.model, animation.clip);
    if (!clip) return fail(error, "model scene animation clip is unavailable");

    animation.time += std::max(delta_seconds, 0.0f);
    bool finished = false;
    if (clip->duration > 0.0f) {
        if (animation.loop) {
            animation.time = std::fmod(animation.time, clip->duration);
        } else if (animation.time >= clip->duration) {
            animation.time = clip->duration;
            finished = true;
        }
    } else if (!animation.loop) {
        animation.time = 0.0f;
        finished = true;
    }

    Models::Runtime::Pose source;
    if (!Models::Runtime::sample(
            animation.model,
            animation.clip,
            animation.time,
            animation.loop,
            &source,
            error)) return false;

    for (AnimationTarget& target : animation.targets) {
        if (!target.instance || target.instance->model == Models::INVALID_MODEL)
            return fail(error, "model scene animation target is unavailable");
        if (!Models::Runtime::retarget(target.binding, source, &target.pose, error)) return false;
        if (!applyPose(world, *target.instance, target.pose, error)) return false;
    }

    for (const AnimationAttachment& attachment : animation.attachments) {
        if (attachment.target >= animation.targets.size() || attachment.source >= source.nodes.size())
            return fail(error, "model scene animation attachment is unavailable");
        AnimationTarget& target = animation.targets[attachment.target];
        if (!target.instance || attachment.target_basis >= target.pose.nodes.size())
            return fail(error, "model scene animation attachment target is unavailable");
        if (!align(
                world,
                *target.instance,
                target.pose,
                attachment.target_basis,
                source.nodes[attachment.source].world,
                error)) return false;
    }

    if (finished) animation.active = false;
    return true;
}

bool playing(const Animation& animation)
{
    return animation.active;
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
    if (instance.pose_entity != Ecs::INVALID_ENTITY && world.alive(instance.pose_entity))
        world.destroyEntity(instance.pose_entity);
    instance = {};
}

} // namespace Renderer::ModelScene
