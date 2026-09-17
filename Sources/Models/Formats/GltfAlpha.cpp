#include "Models/Formats/GltfAlpha.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <algorithm>
#include <string>

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
    if (error) error->clear();
    if (!texture(source)) {
        if (error) *error = "invalid glTF alpha source texture";
        return INVALID_TEXTURE;
    }
    if (mode == AlphaMode::Blend) return source;

    const TextureHandle handle = Internal::registerDeferredAlpha(
        source,
        mode,
        std::clamp(factor, 0.0f, 1.0f),
        std::clamp(cutoff, 0.0f, 1.0f)
    );
    if (handle == INVALID_TEXTURE && error) *error = "failed to register deferred glTF alpha texture";
    return handle;
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
