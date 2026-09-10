#ifndef RW_ENGINE_MODELS_CORE_MATERIAL_HPP
#define RW_ENGINE_MODELS_CORE_MATERIAL_HPP

#include "Models/Core/Texture.hpp"
#include "Models/Core/Types.hpp"

#include <string>
#include <unordered_map>

namespace Models {

struct MaterialData {
    std::string name;

    Vec3 color {1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float roughness = 0.8f;
    float metallic = 0.0f;
    float ambient_occlusion = 1.0f;
    Vec3 emissive_color {0.0f, 0.0f, 0.0f};
    float emissive_strength = 0.0f;
    float ior = 1.5f;
    float clearcoat = 0.0f;
    float clearcoat_roughness = 0.1f;

    std::string texture_path;
    std::string normal_texture_path;
    std::string roughness_texture_path;
    std::string metallic_texture_path;
    std::string ambient_occlusion_texture_path;
    std::string emissive_texture_path;
    std::string opacity_texture_path;

    TextureHandle diffuse_texture = INVALID_TEXTURE;
    TextureHandle normal_texture = INVALID_TEXTURE;
    TextureHandle roughness_texture = INVALID_TEXTURE;
    TextureHandle metallic_texture = INVALID_TEXTURE;
    TextureHandle ambient_occlusion_texture = INVALID_TEXTURE;
    TextureHandle emissive_texture = INVALID_TEXTURE;
    TextureHandle opacity_texture = INVALID_TEXTURE;
};

using MaterialMap = std::unordered_map<std::string, MaterialData>;

bool loadMaterialLibrary(
    const std::string& path,
    MaterialMap *materials,
    std::string *error = nullptr
);

} // namespace Models

#endif
