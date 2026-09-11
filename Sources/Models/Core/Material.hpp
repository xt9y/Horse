#ifndef RW_ENGINE_MODELS_CORE_MATERIAL_HPP
#define RW_ENGINE_MODELS_CORE_MATERIAL_HPP

#include "Models/Core/Texture.hpp"
#include "Models/Core/Types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Models {

enum class AlphaMode : std::uint8_t {
    Opaque,
    Mask,
    Blend,
};

struct SamplerData {
    int mag_filter = 9729;
    int min_filter = 9987;
    int wrap_s = 10497;
    int wrap_t = 10497;
};

struct TextureTransform {
    Vec2 offset {0.0f, 0.0f};
    Vec2 scale {1.0f, 1.0f};
    float rotation = 0.0f;
    int texcoord = -1;
};

struct TextureInfo {
    TextureHandle texture = INVALID_TEXTURE;
    std::uint32_t texcoord = 0u;
    TextureTransform transform{};
    SamplerData sampler{};
    float scale = 1.0f;
};

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
    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    bool unlit = false;

    float normal_scale = 1.0f;
    float specular = 1.0f;
    Vec3 specular_color {1.0f, 1.0f, 1.0f};
    float transmission = 0.0f;
    float thickness = 0.0f;
    float attenuation_distance = 0.0f;
    Vec3 attenuation_color {1.0f, 1.0f, 1.0f};
    Vec3 sheen_color {0.0f, 0.0f, 0.0f};
    float sheen_roughness = 0.0f;
    float anisotropy_strength = 0.0f;
    float anisotropy_rotation = 0.0f;
    float iridescence = 0.0f;
    float iridescence_ior = 1.3f;
    float iridescence_thickness_min = 100.0f;
    float iridescence_thickness_max = 400.0f;
    float dispersion = 0.0f;
    float diffuse_transmission = 0.0f;
    Vec3 diffuse_transmission_color {1.0f, 1.0f, 1.0f};

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

    TextureInfo base_color_info{};
    TextureInfo metallic_roughness_info{};
    TextureInfo normal_info{};
    TextureInfo occlusion_info{};
    TextureInfo emissive_info{};
    TextureInfo clearcoat_info{};
    TextureInfo clearcoat_roughness_info{};
    TextureInfo clearcoat_normal_info{};
    TextureInfo sheen_color_info{};
    TextureInfo sheen_roughness_info{};
    TextureInfo transmission_info{};
    TextureInfo thickness_info{};
    TextureInfo specular_info{};
    TextureInfo specular_color_info{};
    TextureInfo iridescence_info{};
    TextureInfo iridescence_thickness_info{};
    TextureInfo anisotropy_info{};
    TextureInfo diffuse_transmission_info{};
    TextureInfo diffuse_transmission_color_info{};

    std::string extras_json;
    std::unordered_map<std::string, std::string> extensions_json;
};

using MaterialMap = std::unordered_map<std::string, MaterialData>;

bool loadMaterialLibrary(
    const std::string& path,
    MaterialMap *materials,
    std::string *error = nullptr
);

} // namespace Models

#endif
