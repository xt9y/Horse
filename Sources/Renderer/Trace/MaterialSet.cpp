#include "Renderer/Trace/MaterialSet.hpp"

#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Renderer::Trace {
namespace {

struct ChannelSource {
    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    int channel = 0;
    std::uint8_t fallback = 255u;
};

struct PackedTextures {
    Models::TextureHandle metallic_roughness = Models::INVALID_TEXTURE;
    Models::TextureHandle clearcoat = Models::INVALID_TEXTURE;
    Models::TextureHandle sheen = Models::INVALID_TEXTURE;
    Models::TextureHandle transmission_thickness = Models::INVALID_TEXTURE;
    Models::TextureHandle specular = Models::INVALID_TEXTURE;
    Models::TextureHandle iridescence = Models::INVALID_TEXTURE;
    Models::TextureHandle diffuse_transmission = Models::INVALID_TEXTURE;
};

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

std::uint8_t sampleChannel(
    const Models::TextureAsset *asset,
    int channel,
    int x,
    int y,
    int width,
    int height,
    std::uint8_t fallback)
{
    if (!asset || channel < 0 || channel > 3 ||
        asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty())
        return fallback;

    const int source_x = std::clamp(
        static_cast<int>(
            static_cast<long long>(x) * static_cast<long long>(asset->image.width) /
            std::max(width, 1)),
        0,
        asset->image.width - 1);
    const int source_y = std::clamp(
        static_cast<int>(
            static_cast<long long>(y) * static_cast<long long>(asset->image.height) /
            std::max(height, 1)),
        0,
        asset->image.height - 1);
    const std::size_t offset =
        (static_cast<std::size_t>(source_y) * static_cast<std::size_t>(asset->image.width) +
         static_cast<std::size_t>(source_x)) * 4u + static_cast<std::size_t>(channel);
    return offset < asset->image.rgba.size() ? asset->image.rgba[offset] : fallback;
}

Models::TextureHandle packTexture(
    const char *name,
    const std::array<ChannelSource, 4>& channels,
    std::string *error)
{
    std::string key = "@horse-pbr-pack:";
    key += name ? name : "texture";
    for (const ChannelSource& source : channels) {
        key += ":" + std::to_string(source.texture) + "." +
            std::to_string(source.channel) + "." + std::to_string(source.fallback);
    }

    if (const Models::TextureHandle existing = Models::Internal::findTexture(key);
        existing != Models::INVALID_TEXTURE &&
        Models::Internal::textureStorageReady(existing))
        return existing;

    int width = 0;
    int height = 0;
    bool any = false;
    std::array<const Models::TextureAsset *, 4> assets{};

    for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
        const ChannelSource& source = channels[channel];
        if (source.texture == Models::INVALID_TEXTURE) continue;

        if (Models::Internal::textureState(source.texture) != Models::Internal::TextureState::Ready ||
            !Models::Internal::textureStorageReady(source.texture))
            return Models::INVALID_TEXTURE;

        assets[channel] = Models::texture(source.texture);
        if (!assets[channel] || assets[channel]->image.width <= 0 ||
            assets[channel]->image.height <= 0 || assets[channel]->image.rgba.empty()) {
            if (error) *error = "ready PBR texture has no decoded image";
            return Models::INVALID_TEXTURE;
        }

        any = true;
        width = std::max(width, assets[channel]->image.width);
        height = std::max(height, assets[channel]->image.height);
    }
    if (!any) return Models::INVALID_TEXTURE;

    Models::Images::Image image;
    image.width = std::max(width, 1);
    image.height = std::max(height, 1);
    image.rgba.resize(
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u,
        255u);

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t destination =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(x)) * 4u;
            for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
                image.rgba[destination + channel] = sampleChannel(
                    assets[channel],
                    channels[channel].channel,
                    x,
                    y,
                    image.width,
                    image.height,
                    channels[channel].fallback);
            }
        }
    }

    image.meaningful_alpha = channels[3].texture != Models::INVALID_TEXTURE;
    return Models::registerTextureImage(key, std::move(image), error);
}

bool packedTextures(
    const Models::MaterialData& material,
    PackedTextures *packed,
    std::string *error)
{
    if (!packed) return false;
    *packed = {};

    packed->metallic_roughness = packTexture("metallic-roughness", {{
        {material.roughness_texture, 0, 255u},
        {material.metallic_texture, 0, 255u},
        {},
        {},
    }}, error);
    if (error && !error->empty()) return false;

    packed->clearcoat = packTexture("clearcoat", {{
        {material.clearcoat_info.texture, 0, 255u},
        {material.clearcoat_roughness_info.texture, 1, 255u},
        {},
        {},
    }}, error);
    if (error && !error->empty()) return false;

    packed->sheen = packTexture("sheen", {{
        {material.sheen_color_info.texture, 0, 255u},
        {material.sheen_color_info.texture, 1, 255u},
        {material.sheen_color_info.texture, 2, 255u},
        {material.sheen_roughness_info.texture, 3, 255u},
    }}, error);
    if (error && !error->empty()) return false;

    packed->transmission_thickness = packTexture("transmission-thickness", {{
        {material.transmission_info.texture, 0, 255u},
        {material.thickness_info.texture, 1, 255u},
        {},
        {},
    }}, error);
    if (error && !error->empty()) return false;

    packed->specular = packTexture("specular", {{
        {material.specular_color_info.texture, 0, 255u},
        {material.specular_color_info.texture, 1, 255u},
        {material.specular_color_info.texture, 2, 255u},
        {material.specular_info.texture, 3, 255u},
    }}, error);
    if (error && !error->empty()) return false;

    packed->iridescence = packTexture("iridescence", {{
        {material.iridescence_info.texture, 0, 255u},
        {material.iridescence_thickness_info.texture, 1, 255u},
        {},
        {},
    }}, error);
    if (error && !error->empty()) return false;

    packed->diffuse_transmission = packTexture("diffuse-transmission", {{
        {material.diffuse_transmission_color_info.texture, 0, 255u},
        {material.diffuse_transmission_color_info.texture, 1, 255u},
        {material.diffuse_transmission_color_info.texture, 2, 255u},
        {material.diffuse_transmission_info.texture, 3, 255u},
    }}, error);
    if (error && !error->empty()) return false;

    return true;
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

void appendMaterialTextures(
    const Models::MaterialData& material,
    const PackedTextures& packed,
    std::size_t maximum,
    std::vector<Models::TextureHandle>& handles,
    std::unordered_set<Models::TextureHandle>& seen)
{
    const std::array<Models::TextureHandle, 14> candidates {{
        material.diffuse_texture,
        material.normal_texture,
        packed.metallic_roughness,
        material.ambient_occlusion_texture,
        material.emissive_texture,
        material.opacity_texture,
        packed.clearcoat,
        material.clearcoat_normal_info.texture,
        packed.sheen,
        packed.transmission_thickness,
        packed.specular,
        packed.iridescence,
        material.anisotropy_info.texture,
        packed.diffuse_transmission,
    }};
    for (Models::TextureHandle handle : candidates)
        appendTexture(handle, maximum, handles, seen);
}

GpuAdvancedMaterial encode(
    const Models::MaterialData& material,
    const PackedTextures& packed,
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
        std::clamp(material.specular, 0.0f, 1.0f),
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

    const std::int32_t mr = slot(packed.metallic_roughness);
    const std::int32_t coat = slot(packed.clearcoat);
    const std::int32_t sheen = slot(packed.sheen);
    const std::int32_t transmission_thickness = slot(packed.transmission_thickness);
    const std::int32_t specular = slot(packed.specular);
    const std::int32_t iridescence = slot(packed.iridescence);
    const std::int32_t diffuse_transmission = slot(packed.diffuse_transmission);

    gpu.tex0 = {
        slot(material.diffuse_texture),
        slot(material.normal_texture),
        mr,
        mr,
    };
    gpu.tex1 = {
        slot(material.ambient_occlusion_texture),
        slot(material.emissive_texture),
        slot(material.opacity_texture),
        coat,
    };
    gpu.tex2 = {
        coat,
        slot(material.clearcoat_normal_info.texture),
        sheen,
        sheen,
    };
    gpu.tex3 = {
        transmission_thickness,
        transmission_thickness,
        specular,
        specular,
    };
    gpu.tex4 = {
        iridescence,
        iridescence,
        slot(material.anisotropy_info.texture),
        diffuse_transmission,
    };
    gpu.tex5 = {
        diffuse_transmission,
        std::bit_cast<std::int32_t>(std::max(material.clearcoat_normal_info.scale, 0.0f)),
        -1,
        -1,
    };
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

    const std::vector<Models::MaterialHandle> handles = materialHandles(scene);
    std::vector<PackedTextures> packed(handles.size());
    for (std::size_t index = 0u; index < handles.size(); ++index) {
        const Models::MaterialData *material = Models::material(handles[index]);
        if (material && !packedTextures(*material, &packed[index], error)) return false;
    }

    texture_handles_.clear();
    std::unordered_set<Models::TextureHandle> seen;
    if (!scene.textureHandles().empty())
        appendTexture(scene.textureHandles().front(), maximum_texture_slots, texture_handles_, seen);
    base_texture_count_ = texture_handles_.size();

    for (std::size_t index = 0u; index < handles.size(); ++index) {
        const Models::MaterialData *material = Models::material(handles[index]);
        if (material)
            appendMaterialTextures(
                *material,
                packed[index],
                maximum_texture_slots,
                texture_handles_,
                seen);
    }

    std::unordered_map<Models::TextureHandle, std::int32_t> slots;
    slots.reserve(texture_handles_.size());
    for (std::size_t index = 0u; index < texture_handles_.size(); ++index)
        slots.emplace(texture_handles_[index], static_cast<std::int32_t>(index));

    materials_.clear();
    materials_.push_back(GpuAdvancedMaterial{});
    materials_.reserve(handles.size() + 1u);
    for (std::size_t index = 0u; index < handles.size(); ++index) {
        const Models::MaterialData *material = Models::material(handles[index]);
        if (!material) continue;
        materials_.push_back(encode(*material, packed[index], slots));
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
