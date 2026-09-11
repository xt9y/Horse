#include "Models/Formats/Gltf.hpp"
#include "Models/Formats/Registry.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace Models::Formats {
namespace {

TextureHandle maskedTexture(
    TextureHandle source,
    float opacity,
    float cutoff,
    std::string *error)
{
    const TextureAsset *asset = texture(source);
    if (!asset) return INVALID_TEXTURE;

    Images::Image image = asset->image;
    const float safe_opacity = std::clamp(opacity, 0.0f, 1.0f);
    const float safe_cutoff = std::clamp(cutoff, 0.0f, 1.0f);
    for (std::size_t offset = 3u; offset < image.rgba.size(); offset += 4u) {
        const float alpha =
            (static_cast<float>(image.rgba[offset]) / 255.0f) * safe_opacity;
        image.rgba[offset] = alpha >= safe_cutoff ? 255u : 0u;
    }
    image.meaningful_alpha = true;

    const std::uint32_t cutoff_key = static_cast<std::uint32_t>(safe_cutoff * 65535.0f + 0.5f);
    const std::uint32_t opacity_key = static_cast<std::uint32_t>(safe_opacity * 65535.0f + 0.5f);
    return registerTextureImage(
        asset->path + "\n@gltf-alpha:mask:" + std::to_string(cutoff_key) + ":" +
            std::to_string(opacity_key),
        std::move(image),
        error
    );
}

bool normalizeAlpha(MaterialData *material, std::string *error)
{
    if (!material) return false;
    if (material->alpha_mode == AlphaMode::Opaque) {
        material->opacity = 1.0f;
        return true;
    }

    if (material->alpha_mode == AlphaMode::Mask) {
        const float factor = std::clamp(material->opacity, 0.0f, 1.0f);
        if (material->diffuse_texture != INVALID_TEXTURE) {
            const TextureHandle masked = maskedTexture(
                material->diffuse_texture,
                factor,
                material->alpha_cutoff,
                error
            );
            if (masked == INVALID_TEXTURE) return false;
            material->diffuse_texture = masked;
            if (const TextureAsset *asset = texture(masked)) material->texture_path = asset->path;
            material->opacity = 1.0f;
        } else {
            material->opacity = factor >= std::clamp(material->alpha_cutoff, 0.0f, 1.0f)
                ? 1.0f
                : 0.0f;
        }
        return true;
    }

    material->opacity = std::clamp(material->opacity, 0.0f, 1.0f);
    if (material->opacity >= 1.0f && material->diffuse_texture != INVALID_TEXTURE) {
        const TextureAsset *asset = texture(material->diffuse_texture);
        if (asset && asset->image.meaningful_alpha)
            material->opacity = std::nextafter(1.0f, 0.0f);
    }
    return true;
}

bool loadGltf(const std::string& path, Document *output, std::string *error)
{
    if (!Gltf::load(path, output, error)) return false;
    for (Part& part : output->parts) {
        if (!normalizeAlpha(&part.material, error)) return false;
    }
    return true;
}

const Registration gltf_registration(".gltf", loadGltf);
const Registration glb_registration(".glb", loadGltf);

} // namespace
} // namespace Models::Formats
