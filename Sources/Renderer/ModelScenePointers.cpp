#include "Renderer/ModelScenePointers.hpp"

#include "Camera.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/ModelScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace Renderer::ModelScene::Pointers {
namespace {

constexpr float RadToDeg = 57.2957795130823208768f;

struct Target {
    std::uint32_t index = Models::INVALID_INDEX;
    std::string_view property;
};

bool parseTarget(std::string_view pointer, std::string_view prefix, Target *target)
{
    if (!target || !pointer.starts_with(prefix)) return false;
    std::size_t cursor = prefix.size();
    if (cursor >= pointer.size()) return false;

    std::uint64_t index = 0u;
    const std::size_t begin = cursor;
    while (cursor < pointer.size() && pointer[cursor] >= '0' && pointer[cursor] <= '9') {
        index = index * 10u + static_cast<std::uint64_t>(pointer[cursor] - '0');
        if (index > std::numeric_limits<std::uint32_t>::max()) return false;
        ++cursor;
    }
    if (cursor == begin || cursor >= pointer.size() || pointer[cursor] != '/') return false;

    target->index = static_cast<std::uint32_t>(index);
    target->property = pointer.substr(cursor);
    return true;
}

void resetCamera(Ecs::World& world, const Instance& instance, const NodeBinding& binding)
{
    if (binding.entity == Ecs::INVALID_ENTITY) return;
    const Models::NodeData *node = Models::node(instance.model, binding.node);
    if (!node || node->camera == Models::INVALID_INDEX) return;
    Camera::CameraComponent *component = world.get<Camera::CameraComponent>(binding.entity);
    const Models::CameraData *source = Models::camera(instance.model, node->camera);
    if (!component || !source) return;

    const bool active = component->active;
    component->fov_degrees = source->yfov * RadToDeg;
    component->near_plane = source->znear;
    component->far_plane = source->zfar;
    component->aspect_ratio = source->aspect_ratio;
    component->xmag = source->xmag;
    component->ymag = source->ymag;
    component->projection = source->type == Models::CameraType::Orthographic
        ? Camera::Projection::Orthographic
        : Camera::Projection::Perspective;
    component->active = active;
}

void resetLight(Ecs::World& world, const Instance& instance, const NodeBinding& binding)
{
    if (binding.entity == Ecs::INVALID_ENTITY) return;
    const Models::NodeData *node = Models::node(instance.model, binding.node);
    if (!node || node->light == Models::INVALID_INDEX) return;
    LightComponent *component = world.get<LightComponent>(binding.entity);
    const Models::LightData *source = Models::light(instance.model, node->light);
    if (!component || !source) return;

    switch (source->type) {
        case Models::AssetLightType::Directional: component->type = LightType::Directional; break;
        case Models::AssetLightType::Spot: component->type = LightType::Spot; break;
        case Models::AssetLightType::Point:
        default: component->type = LightType::Point; break;
    }
    component->color = {source->color.x, source->color.y, source->color.z};
    component->intensity = source->intensity;
    component->range = source->range;
    component->inner_cone_degrees = source->inner_cone_angle * RadToDeg;
    component->outer_cone_degrees = source->outer_cone_angle * RadToDeg;
}

bool applyCamera(
    Ecs::World& world,
    const Instance& instance,
    const Target& target,
    const std::vector<float>& value)
{
    if (value.empty() || target.index >= Models::cameraCount(instance.model)) return false;
    bool applied = false;
    for (const NodeBinding& binding : instance.nodes) {
        const Models::NodeData *node = Models::node(instance.model, binding.node);
        if (!node || node->camera != target.index || binding.entity == Ecs::INVALID_ENTITY) continue;
        Camera::CameraComponent *camera = world.get<Camera::CameraComponent>(binding.entity);
        if (!camera) continue;

        if (target.property == "/perspective/yfov") camera->fov_degrees = value[0] * RadToDeg;
        else if (target.property == "/perspective/znear") camera->near_plane = value[0];
        else if (target.property == "/perspective/zfar") camera->far_plane = value[0];
        else if (target.property == "/perspective/aspectRatio") camera->aspect_ratio = value[0];
        else if (target.property == "/orthographic/xmag") camera->xmag = value[0];
        else if (target.property == "/orthographic/ymag") camera->ymag = value[0];
        else if (target.property == "/orthographic/znear") camera->near_plane = value[0];
        else if (target.property == "/orthographic/zfar") camera->far_plane = value[0];
        else continue;
        applied = true;
    }
    return applied;
}

bool applyLight(
    Ecs::World& world,
    const Instance& instance,
    const Target& target,
    const std::vector<float>& value)
{
    if (value.empty() || target.index >= Models::lightCount(instance.model)) return false;
    bool applied = false;
    for (const NodeBinding& binding : instance.nodes) {
        const Models::NodeData *node = Models::node(instance.model, binding.node);
        if (!node || node->light != target.index || binding.entity == Ecs::INVALID_ENTITY) continue;
        LightComponent *light = world.get<LightComponent>(binding.entity);
        if (!light) continue;

        if (target.property == "/color" && value.size() >= 3u) {
            light->color = {value[0], value[1], value[2]};
        } else if (target.property == "/intensity") {
            light->intensity = value[0];
        } else if (target.property == "/range") {
            light->range = value[0];
        } else if (target.property == "/spot/innerConeAngle") {
            light->inner_cone_degrees = value[0] * RadToDeg;
        } else if (target.property == "/spot/outerConeAngle") {
            light->outer_cone_degrees = value[0] * RadToDeg;
        } else {
            continue;
        }
        applied = true;
    }
    return applied;
}

} // namespace

bool apply(
    Ecs::World& world,
    Instance& instance,
    const Models::Runtime::Pose& pose,
    std::string *error)
{
    if (error) error->clear();
    if (pose.model != instance.model) {
        if (error) *error = "animation pointer pose does not belong to model scene instance";
        return false;
    }

    for (const NodeBinding& binding : instance.nodes) {
        resetCamera(world, instance, binding);
        resetLight(world, instance, binding);
    }

    for (const auto& [pointer, value] : pose.pointer_values) {
        Target target;
        if (parseTarget(pointer, "/cameras/", &target)) {
            applyCamera(world, instance, target, value);
            continue;
        }
        if (parseTarget(pointer, "/extensions/KHR_lights_punctual/lights/", &target)) {
            applyLight(world, instance, target, value);
            continue;
        }
    }
    return true;
}

} // namespace Renderer::ModelScene::Pointers
