#include "Models/Formats/GltfAlpha.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace Models::Formats::GltfAlpha {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

TextureHandle deriveAlpha(
    TextureHandle source,
    AlphaMode mode,
    float factor,
    float cutoff,
    std::string *error)
{
    const TextureAsset *asset = texture(source);
    if (!asset) return INVALID_TEXTURE;
    if (mode == AlphaMode::Blend) return source;

    Images::Image image = asset->image;
    const float safe_factor = std::clamp(factor, 0.0f, 1.0f);
    const float safe_cutoff = std::clamp(cutoff, 0.0f, 1.0f);
    if (mode == AlphaMode::Opaque) {
        if (!image.meaningful_alpha) return source;
        for (std::size_t offset = 3u; offset < image.rgba.size(); offset += 4u) image.rgba[offset] = 255u;
        image.meaningful_alpha = false;
    } else {
        for (std::size_t offset = 3u; offset < image.rgba.size(); offset += 4u) {
            const float alpha = (static_cast<float>(image.rgba[offset]) / 255.0f) * safe_factor;
            image.rgba[offset] = alpha >= safe_cutoff ? 255u : 0u;
        }
        image.meaningful_alpha = true;
    }

    const std::uint32_t factor_key = static_cast<std::uint32_t>(safe_factor * 65535.0f + 0.5f);
    const std::uint32_t cutoff_key = static_cast<std::uint32_t>(safe_cutoff * 65535.0f + 0.5f);
    const std::string suffix = mode == AlphaMode::Opaque
        ? "opaque"
        : "mask:" + std::to_string(factor_key) + ":" + std::to_string(cutoff_key);
    return registerTextureImage(asset->path + "\n@gltf-alpha:" + suffix, std::move(image), error);
}

bool material(MaterialData *value, std::string *error)
{
    if (!value) return false;
    const float factor = std::clamp(value->opacity, 0.0f, 1.0f);
    const TextureHandle original = value->base_color_info.texture != INVALID_TEXTURE
        ? value->base_color_info.texture
        : value->diffuse_texture;

    if (value->alpha_mode == AlphaMode::Opaque) {
        value->opacity = 1.0f;
        if (original != INVALID_TEXTURE) {
            value->diffuse_texture = deriveAlpha(original, AlphaMode::Opaque, 1.0f, 0.0f, error);
            if (value->diffuse_texture == INVALID_TEXTURE) return false;
        }
    } else if (value->alpha_mode == AlphaMode::Mask) {
        if (original != INVALID_TEXTURE) {
            value->diffuse_texture = deriveAlpha(original, AlphaMode::Mask, factor, value->alpha_cutoff, error);
            if (value->diffuse_texture == INVALID_TEXTURE) return false;
            value->opacity = 1.0f;
        } else {
            value->opacity = factor >= std::clamp(value->alpha_cutoff, 0.0f, 1.0f) ? 1.0f : 0.0f;
        }
    } else {
        value->opacity = factor;
        if (original != INVALID_TEXTURE) value->diffuse_texture = original;
        if (value->opacity >= 1.0f && original != INVALID_TEXTURE) {
            const TextureAsset *asset = texture(original);
            if (asset && asset->image.meaningful_alpha) value->opacity = std::nextafter(1.0f, 0.0f);
        }
    }

    if (value->diffuse_texture != INVALID_TEXTURE) {
        if (const TextureAsset *asset = texture(value->diffuse_texture)) value->texture_path = asset->path;
    }
    return true;
}

} // namespace

bool apply(Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) return fail(error, "null glTF alpha document");
    for (Part& part : document->parts)
        if (!material(&part.material, error)) return false;
    for (VariantMaterial& variant : document->variant_materials)
        if (!material(&variant.material, error)) return false;
    return true;
}

} // namespace Models::Formats::GltfAlpha
