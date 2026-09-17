#include "Models/Internal/StagedModel.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace Models::Internal {
namespace {

constexpr TextureHandle StagedTextureBit = 0x80000000u;
constexpr TextureHandle StagedTextureIndexMask = 0x7fffffffu;
thread_local StagedModel *active_staging = nullptr;

Images::Image fallbackImage()
{
    Images::Image image;
    image.width = 1;
    image.height = 1;
    image.rgba = {255u, 255u, 255u, 255u};
    image.meaningful_alpha = false;
    return image;
}

std::size_t stagedIndex(TextureHandle handle)
{
    return static_cast<std::size_t>(handle & StagedTextureIndexMask);
}

bool remapHandle(
    TextureHandle *handle,
    const std::unordered_map<TextureHandle, TextureHandle>& mapping,
    std::string *error)
{
    if (!handle || *handle == INVALID_TEXTURE || !isStagedTextureHandle(*handle)) return true;
    const auto found = mapping.find(*handle);
    if (found == mapping.end() || found->second == INVALID_TEXTURE) {
        if (error) *error = "staged material references an unpublished texture";
        return false;
    }
    *handle = found->second;
    return true;
}

bool remapMaterial(
    MaterialData& material,
    const std::unordered_map<TextureHandle, TextureHandle>& mapping,
    std::string *error)
{
    TextureHandle *legacy[] = {
        &material.diffuse_texture,
        &material.normal_texture,
        &material.roughness_texture,
        &material.metallic_texture,
        &material.ambient_occlusion_texture,
        &material.emissive_texture,
        &material.opacity_texture,
    };
    for (TextureHandle *handle : legacy)
        if (!remapHandle(handle, mapping, error)) return false;

    TextureInfo *infos[] = {
        &material.base_color_info,
        &material.metallic_roughness_info,
        &material.normal_info,
        &material.occlusion_info,
        &material.emissive_info,
        &material.clearcoat_info,
        &material.clearcoat_roughness_info,
        &material.clearcoat_normal_info,
        &material.sheen_color_info,
        &material.sheen_roughness_info,
        &material.transmission_info,
        &material.thickness_info,
        &material.specular_info,
        &material.specular_color_info,
        &material.iridescence_info,
        &material.iridescence_thickness_info,
        &material.anisotropy_info,
        &material.diffuse_transmission_info,
        &material.diffuse_transmission_color_info,
    };
    for (TextureInfo *info : infos)
        if (!remapHandle(&info->texture, mapping, error)) return false;
    return true;
}

} // namespace

StagingScope::StagingScope(StagedModel& model)
    : previous_(active_staging)
{
    active_staging = &model;
}

StagingScope::~StagingScope()
{
    active_staging = previous_;
}

bool stagingActive()
{
    return active_staging != nullptr;
}

bool isStagedTextureHandle(TextureHandle handle)
{
    return handle != INVALID_TEXTURE && (handle & StagedTextureBit) != 0u;
}

TextureHandle registerStagedTexture(TextureSourceDescriptor descriptor)
{
    if (!active_staging || descriptor.key.empty()) return INVALID_TEXTURE;
    if (const auto found = active_staging->texture_lookup.find(descriptor.key);
        found != active_staging->texture_lookup.end())
        return found->second;

    if (active_staging->textures.size() >= static_cast<std::size_t>(StagedTextureIndexMask))
        return INVALID_TEXTURE;
    const TextureHandle handle = StagedTextureBit |
        static_cast<TextureHandle>(active_staging->textures.size());
    if (handle == INVALID_TEXTURE) return INVALID_TEXTURE;

    StagedTexture texture;
    texture.asset.path = descriptor.key;
    texture.asset.image = fallbackImage();
    texture.descriptor = std::move(descriptor);
    active_staging->textures.push_back(std::move(texture));
    active_staging->texture_lookup.emplace(active_staging->textures.back().asset.path, handle);
    return handle;
}

TextureHandle findStagedTexture(const std::string& key)
{
    if (!active_staging) return INVALID_TEXTURE;
    const auto found = active_staging->texture_lookup.find(key);
    return found == active_staging->texture_lookup.end() ? INVALID_TEXTURE : found->second;
}

const TextureAsset *stagedTexture(TextureHandle handle)
{
    if (!active_staging || !isStagedTextureHandle(handle)) return nullptr;
    const std::size_t index = stagedIndex(handle);
    return index < active_staging->textures.size()
        ? &active_staging->textures[index].asset
        : nullptr;
}

bool stagedTextureDescriptor(TextureHandle handle, TextureSourceDescriptor *descriptor)
{
    if (!active_staging || !descriptor || !isStagedTextureHandle(handle)) return false;
    const std::size_t index = stagedIndex(handle);
    if (index >= active_staging->textures.size()) return false;
    *descriptor = active_staging->textures[index].descriptor;
    return true;
}

bool materializeStagedTextures(
    const StagedModel& staged,
    std::unordered_map<TextureHandle, TextureHandle> *mapping,
    std::string *error)
{
    if (error) error->clear();
    if (!mapping) {
        if (error) *error = "null staged texture mapping";
        return false;
    }
    mapping->clear();
    mapping->reserve(staged.textures.size());

    for (std::size_t index = 0u; index < staged.textures.size(); ++index) {
        const TextureHandle staged_handle = StagedTextureBit | static_cast<TextureHandle>(index);
        TextureSourceDescriptor descriptor = staged.textures[index].descriptor;

        const auto remap_dependency = [&](TextureHandle *handle) -> bool {
            if (!handle || *handle == INVALID_TEXTURE || !isStagedTextureHandle(*handle)) return true;
            const auto found = mapping->find(*handle);
            if (found == mapping->end()) {
                if (error) *error = "staged texture dependency is not topologically ordered";
                return false;
            }
            *handle = found->second;
            return true;
        };

        if (!remap_dependency(&descriptor.source) || !remap_dependency(&descriptor.secondary))
            return false;
        const TextureHandle published = registerDeferredDescriptor(std::move(descriptor));
        if (published == INVALID_TEXTURE) {
            if (error) *error = "failed to publish staged texture descriptor";
            return false;
        }
        mapping->emplace(staged_handle, published);
    }
    return true;
}

bool remapDocumentTextures(
    Formats::Document& document,
    const std::unordered_map<TextureHandle, TextureHandle>& mapping,
    std::string *error)
{
    if (error) error->clear();
    for (Formats::Part& part : document.parts)
        if (!remapMaterial(part.material, mapping, error)) return false;
    for (Formats::VariantMaterial& variant : document.variant_materials)
        if (!remapMaterial(variant.material, mapping, error)) return false;
    return true;
}

} // namespace Models::Internal
