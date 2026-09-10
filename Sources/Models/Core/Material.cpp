#include "Models/Core/Material.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace Models {
namespace {

std::string textureValue(const std::string& value)
{
    if (value.empty() || value[0] != '-') return value;

    std::istringstream stream(value);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens.empty() ? std::string{} : tokens.back();
}

bool detectLegacyZeroDIsOpaque(const MaterialMap& materials)
{
    std::size_t ordinary_textured = 0u;
    std::size_t ordinary_zero = 0u;
    std::size_t alpha_materials = 0u;
    bool alpha_scalar_convention = true;

    for (const auto& [name, material] : materials) {
        (void)name;

        if (!material.opacity_texture_path.empty()) {
            ++alpha_materials;
            if (material.opacity < 0.999f) alpha_scalar_convention = false;
            continue;
        }

        if (material.texture_path.empty()) continue;
        ++ordinary_textured;
        if (material.opacity <= 0.0001f) ++ordinary_zero;
    }

    return ordinary_textured > 0u
        && ordinary_zero * 100u >= ordinary_textured * 80u
        && alpha_materials > 0u
        && alpha_scalar_convention;
}

bool loadMap(
    const std::filesystem::path& material_path,
    const std::string& value,
    std::string *destination_path,
    TextureHandle *destination,
    std::string *first_error)
{
    if (!destination_path || !destination) return false;
    const std::string filename = textureValue(value);
    if (filename.empty()) return false;

    *destination_path = (material_path.parent_path() / filename).lexically_normal().string();
    std::string texture_error;
    *destination = loadTexture(*destination_path, &texture_error);
    if (*destination == INVALID_TEXTURE && first_error && first_error->empty()) {
        *first_error = "failed to load texture '" + *destination_path + "'";
        if (!texture_error.empty()) *first_error += ": " + texture_error;
    }
    return true;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

bool loadMaterialLibrary(
    const std::string& path,
    MaterialMap *materials,
    std::string *error)
{
    if (error) error->clear();
    if (!materials) {
        if (error) *error = "null material destination";
        return false;
    }
    materials->clear();

    std::ifstream input(path);
    if (!input) {
        if (error) *error = "cannot open material library: " + path;
        return false;
    }

    const std::filesystem::path material_path(path);
    MaterialData *current = nullptr;
    std::string line;
    std::string texture_error;

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string key;
        stream >> key;
        if (key.empty() || key[0] == '#') continue;

        if (key == "newmtl") {
            std::string name;
            std::getline(stream >> std::ws, name);
            if (name.empty()) continue;
            current = &(*materials)[name];
            current->name = name;
            continue;
        }
        if (!current) continue;

        if (key == "Kd") {
            stream >> current->color.x >> current->color.y >> current->color.z;
        } else if (key == "Ke") {
            stream >> current->emissive_color.x >> current->emissive_color.y >> current->emissive_color.z;
            const float peak = std::max({
                current->emissive_color.x,
                current->emissive_color.y,
                current->emissive_color.z,
            });
            if (peak > 0.0f && current->emissive_strength <= 0.0f) current->emissive_strength = 1.0f;
        } else if (key == "d") {
            stream >> current->opacity;
            current->opacity = clamp01(current->opacity);
        } else if (key == "Tr") {
            float transparency = 0.0f;
            stream >> transparency;
            current->opacity = 1.0f - clamp01(transparency);
        } else if (key == "Pr") {
            stream >> current->roughness;
            current->roughness = clamp01(current->roughness);
        } else if (key == "Pm") {
            stream >> current->metallic;
            current->metallic = clamp01(current->metallic);
        } else if (key == "Ni") {
            stream >> current->ior;
            current->ior = std::max(current->ior, 1.0f);
        } else if (key == "Pc") {
            stream >> current->clearcoat;
            current->clearcoat = clamp01(current->clearcoat);
        } else if (key == "Pcr") {
            stream >> current->clearcoat_roughness;
            current->clearcoat_roughness = clamp01(current->clearcoat_roughness);
        } else if (key == "Ns") {
            float shininess = 0.0f;
            stream >> shininess;
            if (shininess >= 0.0f) {
                current->roughness = clamp01(std::sqrt(2.0f / (shininess + 2.0f)));
            }
        } else if (key == "map_Kd") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(material_path, value, &current->texture_path, &current->diffuse_texture, &texture_error);
        } else if (key == "map_Pr") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(material_path, value, &current->roughness_texture_path, &current->roughness_texture, &texture_error);
        } else if (key == "map_Pm") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(material_path, value, &current->metallic_texture_path, &current->metallic_texture, &texture_error);
        } else if (key == "map_Ke") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(material_path, value, &current->emissive_texture_path, &current->emissive_texture, &texture_error);
            if (current->emissive_texture != INVALID_TEXTURE && current->emissive_strength <= 0.0f)
                current->emissive_strength = 1.0f;
        } else if (key == "map_AO" || key == "map_ao") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(
                material_path,
                value,
                &current->ambient_occlusion_texture_path,
                &current->ambient_occlusion_texture,
                &texture_error
            );
        } else if (key == "norm" || key == "map_Kn" || key == "map_Bump" || key == "bump") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(material_path, value, &current->normal_texture_path, &current->normal_texture, &texture_error);
        } else if (key == "map_d") {
            std::string value;
            std::getline(stream >> std::ws, value);
            loadMap(material_path, value, &current->opacity_texture_path, &current->opacity_texture, &texture_error);
        }
    }

    if (detectLegacyZeroDIsOpaque(*materials)) {
        for (auto& [name, material] : *materials) {
            (void)name;
            if (material.opacity <= 0.0001f && material.opacity_texture_path.empty()) {
                material.opacity = 1.0f;
            }
        }
    }

    for (auto& [name, material] : *materials) {
        (void)name;
        if (material.texture_path.empty() || material.opacity_texture_path.empty()) continue;

        std::string masked_error;
        const TextureHandle masked = loadTextureWithOpacity(
            material.texture_path,
            material.opacity_texture_path,
            &masked_error
        );
        if (masked != INVALID_TEXTURE) material.diffuse_texture = masked;
        else if (texture_error.empty()) {
            texture_error = "failed to combine base-color and opacity textures";
            if (!masked_error.empty()) texture_error += ": " + masked_error;
        }
    }

    if (!texture_error.empty()) {
        if (error) *error = texture_error;
        return false;
    }

    return true;
}

} // namespace Models
