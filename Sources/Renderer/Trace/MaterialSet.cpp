#include "Renderer/Trace/MaterialSet.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace Renderer::Trace {
namespace {

void appendTexture(
    Models::TextureHandle handle,
    std::size_t maximum,
    std::vector<Models::TextureHandle>& handles,
    std::unordered_set<Models::TextureHandle>& seen)
{
    if (handle == Models::INVALID_TEXTURE || seen.contains(handle) || handles.size() >= maximum) return;
    seen.insert(handle);
    handles.push_back(handle);
}

std::vector<Models::MaterialHandle> materialHandles(const Scenes::SceneCache& scene)
{
    std::vector<Models::MaterialHandle> handles;
    std::unordered_set<Models::MaterialHandle> seen;
    for (const Scenes::Scene::RenderItem& item : scene.renderItems()) {
        if (!item.mesh_component || !item.material ||
            item.material->opacity < Scenes::SceneCache::opacityCutoff())
            continue;
        const Models::MaterialHandle handle = item.mesh_component->material;
        if (handle != Models::INVALID_MATERIAL && seen.insert(handle).second)
            handles.push_back(handle);
    }
    std::sort(handles.begin(), handles.end());
    return handles;
}

void appendAdvancedTextures(
    const Models::MaterialData& material,
    std::size_t maximum,
    std::vector<Models::TextureHandle>& handles,
    std::unordered_set<Models::TextureHandle>& seen)
{
    const std::array<Models::TextureHandle, 21> candidates {{
        material.diffuse_texture,
        material.normal_texture,
        material.roughness_texture,
        material.metallic_texture,
        material.ambient_occlusion_texture,
        material.emissive_texture,
        material.opacity_texture,
        material.clearcoat_info.texture,
        material.clearcoat_roughness_info.texture,
        material.clearcoat_normal_info.texture,
        material.sheen_color_info.texture,
        material.sheen_roughness_info.texture,
        material.transmission_info.texture,
        material.thickness_info.texture,
        material.specular_info.texture,
        material.specular_color_info.texture,
        material.iridescence_info.texture,
        material.iridescence_thickness_info.texture,
        material.anisotropy_info.texture,
        material.diffuse_transmission_info.texture,
        material.diffuse_transmission_color_info.texture,
    }};
    for (Models::TextureHandle handle : candidates)
        appendTexture(handle, maximum, handles, seen);
}

GpuAdvancedMaterial encode(
    const Models::MaterialData& material,
    const std::unordered_map<Models::TextureHandle, std::int32_t>& slots)
{
    auto slot = [&](Models::TextureHandle handle) -> std::int32_t {
        if (handle == Models::INVALID_TEXTURE) return -1;
        const auto found = slots.find(handle);
        return found == slots.end() ? -1 : found->second;
    };

    GpuAdvancedMaterial gpu;
    gpu.emissive_strength = {
        material.emissive_color.x,
        material.emissive_color.y,
        material.emissive_color.z,
        std::max(material.emissive_strength, 0.0f),
    };
    gpu.pbr = {
        std::clamp(material.roughness, 0.04f, 1.0f),
        std::clamp(material.metallic, 0.0f, 1.0f),
        std::clamp(material.ambient_occlusion, 0.0f, 1.0f),
        std::max(material.normal_scale, 0.0f),
    };
    gpu.specular = {
        std::max(material.specular, 0.0f),
        material.specular_color.x,
        material.specular_color.y,
        material.specular_color.z,
    };
    gpu.clearcoat_sheen = {
        std::clamp(material.clearcoat, 0.0f, 1.0f),
        std::clamp(material.clearcoat_roughness, 0.0f, 1.0f),
        std::clamp(material.sheen_roughness, 0.0f, 1.0f),
        std::clamp(material.transmission, 0.0f, 1.0f),
    };
    gpu.sheen_thickness = {
        material.sheen_color.x,
        material.sheen_color.y,
        material.sheen_color.z,
        std::max(material.thickness, 0.0f),
    };
    gpu.attenuation = {
        material.attenuation_color.x,
        material.attenuation_color.y,
        material.attenuation_color.z,
        std::max(material.attenuation_distance, 0.0f),
    };
    gpu.diffuse_transmission = {
        material.diffuse_transmission_color.x,
        material.diffuse_transmission_color.y,
        material.diffuse_transmission_color.z,
        std::clamp(material.diffuse_transmission, 0.0f, 1.0f),
    };
    gpu.anisotropy_iridescence = {
        std::clamp(material.anisotropy_strength, 0.0f, 1.0f),
        material.anisotropy_rotation,
        std::clamp(material.iridescence, 0.0f, 1.0f),
        std::max(material.iridescence_ior, 1.0f),
    };
    gpu.iridescence_dispersion_ior = {
        std::max(material.iridescence_thickness_min, 0.0f),
        std::max(material.iridescence_thickness_max, material.iridescence_thickness_min),
        std::max(material.dispersion, 0.0f),
        std::max(material.ior, 1.0001f),
    };
    gpu.misc = {
        std::clamp(material.alpha_cutoff, 0.0f, 1.0f),
        material.unlit ? 1.0f : 0.0f,
        material.double_sided ? 1.0f : 0.0f,
        static_cast<float>(static_cast<std::uint8_t>(material.alpha_mode)),
    };
    gpu.tex0 = {
        slot(material.diffuse_texture), slot(material.normal_texture),
        slot(material.roughness_texture), slot(material.metallic_texture),
    };
    gpu.tex1 = {
        slot(material.ambient_occlusion_texture), slot(material.emissive_texture),
        slot(material.opacity_texture), slot(material.clearcoat_info.texture),
    };
    gpu.tex2 = {
        slot(material.clearcoat_roughness_info.texture), slot(material.clearcoat_normal_info.texture),
        slot(material.sheen_color_info.texture), slot(material.sheen_roughness_info.texture),
    };
    gpu.tex3 = {
        slot(material.transmission_info.texture), slot(material.thickness_info.texture),
        slot(material.specular_info.texture), slot(material.specular_color_info.texture),
    };
    gpu.tex4 = {
        slot(material.iridescence_info.texture), slot(material.iridescence_thickness_info.texture),
        slot(material.anisotropy_info.texture), slot(material.diffuse_transmission_info.texture),
    };
    gpu.tex5 = {slot(material.diffuse_transmission_color_info.texture), -1, -1, -1};
    return gpu;
}

} // namespace

bool MaterialSet::sync(
    const Scenes::SceneCache& scene,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (error) error->clear();
    if (source_revision_ == scene.resourceRevision() && !materials_.empty()) return true;

    texture_handles_ = scene.textureHandles();
    if (texture_handles_.size() > maximum_texture_slots) {
        if (error) *error = "trace material base texture count exceeds backend capacity";
        return false;
    }
    base_texture_count_ = texture_handles_.size();
    std::unordered_set<Models::TextureHandle> seen(texture_handles_.begin(), texture_handles_.end());
    const std::vector<Models::MaterialHandle> handles = materialHandles(scene);
    for (Models::MaterialHandle handle : handles) {
        const Models::MaterialData *material = Models::material(handle);
        if (material) appendAdvancedTextures(*material, maximum_texture_slots, texture_handles_, seen);
    }

    std::unordered_map<Models::TextureHandle, std::int32_t> slots;
    slots.reserve(texture_handles_.size());
    for (std::size_t index = 0u; index < texture_handles_.size(); ++index)
        slots.emplace(texture_handles_[index], static_cast<std::int32_t>(index));

    materials_.clear();
    materials_.push_back(GpuAdvancedMaterial{});
    materials_.reserve(handles.size() + 1u);
    for (Models::MaterialHandle handle : handles) {
        const Models::MaterialData *material = Models::material(handle);
        if (!material) continue;
        materials_.push_back(encode(*material, slots));
    }

    source_revision_ = scene.resourceRevision();
    ++revision_;
    return true;
}

void MaterialSet::clear()
{
    materials_.clear();
    texture_handles_.clear();
    base_texture_count_ = 0u;
    source_revision_ = UINT64_MAX;
    ++revision_;
}

} // namespace Renderer::Trace
