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

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
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

std::uint32_t sourceMaterialForVariant(
    Models::ModelHandle model,
    std::size_t part_index,
    std::uint32_t variant)
{
    const Models::ModelPart *part = Models::part(model, part_index);
    if (!part) return Models::INVALID_INDEX;
    if (variant == Models::INVALID_INDEX) return part->source_material;

    for (std::size_t index = 0u; index < Models::variantMappingCount(model); ++index) {
        const Models::VariantMappingData *mapping = Models::variantMapping(model, index);
        if (!mapping || mapping->part != part_index) continue;
        if (std::find(mapping->variants.begin(), mapping->variants.end(), variant) != mapping->variants.end())
            return mapping->source_material;
    }
    return part->source_material;
}

template <typename Function>
void eachPart(Instance& instance, Function&& function)
{
    for (NodeBinding& node : instance.nodes)
        for (PartBinding& part : node.parts)
            function(part);
    for (PartBinding& part : instance.loose_parts) function(part);
}

bool hasMaterialPointers(const Models::Runtime::Pose& pose)
{
    for (const auto& [pointer, value] : pose.pointer_values) {
        (void)value;
        if (pointer.starts_with("/materials/")) return true;
    }
    return false;
}

bool resetMaterials(Ecs::World& world, Instance& instance, bool reset_clones)
{
    bool changed = false;
    eachPart(instance, [&](PartBinding& binding) {
        MeshComponent *mesh = world.get<MeshComponent>(binding.entity);
        if (!mesh) return;
        const Models::MaterialHandle base = Models::Runtime::materialForVariant(
            instance.model,
            binding.part,
            instance.variant
        );
        if (base == Models::INVALID_MATERIAL) return;
        if (mesh->material != base) {
            mesh->material = base;
            changed = true;
        }
        if (!reset_clones || binding.animated_material == Models::INVALID_MATERIAL) return;
        const Models::MaterialData *source = Models::material(base);
        if (source && Models::updateMaterial(binding.animated_material, *source)) changed = true;
    });
    return changed;
}

bool applyTextureInfo(
    Models::TextureInfo *info,
    std::string_view property,
    std::string_view prefix,
    const std::vector<float>& value)
{
    if (!info || value.empty() || !property.starts_with(prefix)) return false;
    const std::string_view tail = property.substr(prefix.size());
    if (tail == "/texCoord") {
        info->texcoord = static_cast<std::uint32_t>(std::max(std::lround(value[0]), 0l));
        return true;
    }
    if (tail == "/extensions/KHR_texture_transform/offset" && value.size() >= 2u) {
        info->transform.offset = {value[0], value[1]};
        return true;
    }
    if (tail == "/extensions/KHR_texture_transform/scale" && value.size() >= 2u) {
        info->transform.scale = {value[0], value[1]};
        return true;
    }
    if (tail == "/extensions/KHR_texture_transform/rotation") {
        info->transform.rotation = value[0];
        return true;
    }
    if (tail == "/extensions/KHR_texture_transform/texCoord") {
        info->transform.texcoord = static_cast<int>(std::max(std::lround(value[0]), 0l));
        return true;
    }
    return false;
}

bool applyMaterialProperty(
    Models::MaterialData *material,
    std::string_view property,
    const std::vector<float>& value)
{
    if (!material || value.empty()) return false;

    if (property == "/pbrMetallicRoughness/baseColorFactor" && value.size() >= 4u) {
        material->color = {value[0], value[1], value[2]};
        material->opacity = value[3];
        return true;
    }
    if (property == "/pbrMetallicRoughness/metallicFactor") {
        material->metallic = clamp01(value[0]);
        return true;
    }
    if (property == "/pbrMetallicRoughness/roughnessFactor") {
        material->roughness = clamp01(value[0]);
        return true;
    }
    if (property == "/normalTexture/scale") {
        material->normal_scale = value[0];
        material->normal_info.scale = value[0];
        return true;
    }
    if (property == "/occlusionTexture/strength") {
        material->ambient_occlusion = clamp01(value[0]);
        material->occlusion_info.scale = value[0];
        return true;
    }
    if (property == "/emissiveFactor" && value.size() >= 3u) {
        material->emissive_color = {value[0], value[1], value[2]};
        return true;
    }
    if (property == "/alphaCutoff") {
        material->alpha_cutoff = value[0];
        return true;
    }

    if (property == "/extensions/KHR_materials_emissive_strength/emissiveStrength") {
        material->emissive_strength = std::max(value[0], 0.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_ior/ior") {
        material->ior = std::max(value[0], 1.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_clearcoat/clearcoatFactor") {
        material->clearcoat = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_clearcoat/clearcoatRoughnessFactor") {
        material->clearcoat_roughness = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_sheen/sheenColorFactor" && value.size() >= 3u) {
        material->sheen_color = {value[0], value[1], value[2]};
        return true;
    }
    if (property == "/extensions/KHR_materials_sheen/sheenRoughnessFactor") {
        material->sheen_roughness = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_specular/specularFactor") {
        material->specular = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_specular/specularColorFactor" && value.size() >= 3u) {
        material->specular_color = {value[0], value[1], value[2]};
        return true;
    }
    if (property == "/extensions/KHR_materials_transmission/transmissionFactor") {
        material->transmission = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_volume/thicknessFactor") {
        material->thickness = std::max(value[0], 0.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_volume/attenuationDistance") {
        material->attenuation_distance = std::max(value[0], 0.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_volume/attenuationColor" && value.size() >= 3u) {
        material->attenuation_color = {value[0], value[1], value[2]};
        return true;
    }
    if (property == "/extensions/KHR_materials_iridescence/iridescenceFactor") {
        material->iridescence = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_iridescence/iridescenceIor") {
        material->iridescence_ior = std::max(value[0], 1.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_iridescence/iridescenceThicknessMinimum") {
        material->iridescence_thickness_min = std::max(value[0], 0.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_iridescence/iridescenceThicknessMaximum") {
        material->iridescence_thickness_max = std::max(value[0], 0.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_anisotropy/anisotropyStrength") {
        material->anisotropy_strength = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_anisotropy/anisotropyRotation") {
        material->anisotropy_rotation = value[0];
        return true;
    }
    if (property == "/extensions/KHR_materials_dispersion/dispersion") {
        material->dispersion = std::max(value[0], 0.0f);
        return true;
    }
    if (property == "/extensions/KHR_materials_diffuse_transmission/diffuseTransmissionFactor") {
        material->diffuse_transmission = clamp01(value[0]);
        return true;
    }
    if (property == "/extensions/KHR_materials_diffuse_transmission/diffuseTransmissionColorFactor" && value.size() >= 3u) {
        material->diffuse_transmission_color = {value[0], value[1], value[2]};
        return true;
    }

    if (applyTextureInfo(&material->base_color_info, property, "/pbrMetallicRoughness/baseColorTexture", value)) return true;
    if (applyTextureInfo(&material->metallic_roughness_info, property, "/pbrMetallicRoughness/metallicRoughnessTexture", value)) return true;
    if (applyTextureInfo(&material->normal_info, property, "/normalTexture", value)) return true;
    if (applyTextureInfo(&material->occlusion_info, property, "/occlusionTexture", value)) return true;
    if (applyTextureInfo(&material->emissive_info, property, "/emissiveTexture", value)) return true;
    if (applyTextureInfo(&material->clearcoat_info, property, "/extensions/KHR_materials_clearcoat/clearcoatTexture", value)) return true;
    if (applyTextureInfo(&material->clearcoat_roughness_info, property, "/extensions/KHR_materials_clearcoat/clearcoatRoughnessTexture", value)) return true;
    if (applyTextureInfo(&material->clearcoat_normal_info, property, "/extensions/KHR_materials_clearcoat/clearcoatNormalTexture", value)) return true;
    if (applyTextureInfo(&material->sheen_color_info, property, "/extensions/KHR_materials_sheen/sheenColorTexture", value)) return true;
    if (applyTextureInfo(&material->sheen_roughness_info, property, "/extensions/KHR_materials_sheen/sheenRoughnessTexture", value)) return true;
    if (applyTextureInfo(&material->specular_info, property, "/extensions/KHR_materials_specular/specularTexture", value)) return true;
    if (applyTextureInfo(&material->specular_color_info, property, "/extensions/KHR_materials_specular/specularColorTexture", value)) return true;
    if (applyTextureInfo(&material->transmission_info, property, "/extensions/KHR_materials_transmission/transmissionTexture", value)) return true;
    if (applyTextureInfo(&material->thickness_info, property, "/extensions/KHR_materials_volume/thicknessTexture", value)) return true;
    if (applyTextureInfo(&material->iridescence_info, property, "/extensions/KHR_materials_iridescence/iridescenceTexture", value)) return true;
    if (applyTextureInfo(&material->iridescence_thickness_info, property, "/extensions/KHR_materials_iridescence/iridescenceThicknessTexture", value)) return true;
    if (applyTextureInfo(&material->anisotropy_info, property, "/extensions/KHR_materials_anisotropy/anisotropyTexture", value)) return true;
    if (applyTextureInfo(&material->diffuse_transmission_info, property, "/extensions/KHR_materials_diffuse_transmission/diffuseTransmissionTexture", value)) return true;
    if (applyTextureInfo(&material->diffuse_transmission_color_info, property, "/extensions/KHR_materials_diffuse_transmission/diffuseTransmissionColorTexture", value)) return true;
    return false;
}

bool applyMaterial(
    Ecs::World& world,
    Instance& instance,
    const Target& target,
    const std::vector<float>& value)
{
    if (value.empty()) return false;
    bool applied = false;
    eachPart(instance, [&](PartBinding& binding) {
        if (sourceMaterialForVariant(instance.model, binding.part, instance.variant) != target.index) return;
        MeshComponent *mesh = world.get<MeshComponent>(binding.entity);
        if (!mesh) return;
        const Models::MaterialHandle base = Models::Runtime::materialForVariant(
            instance.model,
            binding.part,
            instance.variant
        );
        const Models::MaterialData *base_material = Models::material(base);
        if (!base_material) return;

        if (binding.animated_material == Models::INVALID_MATERIAL) {
            binding.animated_material = Models::registerMaterial(*base_material);
            if (binding.animated_material == Models::INVALID_MATERIAL) return;
        }
        const Models::MaterialData *current = Models::material(binding.animated_material);
        if (!current) return;
        Models::MaterialData replacement = *current;
        if (!applyMaterialProperty(&replacement, target.property, value)) return;
        if (!Models::updateMaterial(binding.animated_material, replacement)) return;
        mesh->material = binding.animated_material;
        applied = true;
    });
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

    const bool material_pointers = hasMaterialPointers(pose);
    bool resources_changed = resetMaterials(world, instance, material_pointers);

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
        if (parseTarget(pointer, "/materials/", &target)) {
            resources_changed = applyMaterial(world, instance, target, value) || resources_changed;
            continue;
        }
    }

    if (resources_changed) world.markChanged(Ecs::ChangeKind::Resource);
    return true;
}

} // namespace Renderer::ModelScene::Pointers
