#include "Models/Core/Material.hpp"

#include <algorithm>
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
    std::string diffuse_error;

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
        } else if (key == "d") {
            stream >> current->opacity;
            current->opacity = std::clamp(current->opacity, 0.0f, 1.0f);
        } else if (key == "Tr") {
            float transparency = 0.0f;
            stream >> transparency;
            current->opacity = 1.0f - std::clamp(transparency, 0.0f, 1.0f);
        } else if (key == "map_Kd") {
            std::string value;
            std::getline(stream >> std::ws, value);
            value = textureValue(value);
            if (value.empty()) continue;

            const std::filesystem::path texture_path =
                (material_path.parent_path() / value).lexically_normal();
            current->texture_path = texture_path.string();

            std::string texture_error;
            current->diffuse_texture = loadTexture(current->texture_path, &texture_error);
            if (current->diffuse_texture == INVALID_TEXTURE && diffuse_error.empty()) {
                diffuse_error = "failed to load diffuse texture '" + current->texture_path + "'";
                if (!texture_error.empty()) diffuse_error += ": " + texture_error;
            }
        } else if (key == "map_d") {
            std::string value;
            std::getline(stream >> std::ws, value);
            value = textureValue(value);
            if (value.empty()) continue;

            current->opacity_texture_path =
                (material_path.parent_path() / value).lexically_normal().string();
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

        std::string texture_error;
        const TextureHandle masked = loadTextureWithOpacity(
            material.texture_path,
            material.opacity_texture_path,
            &texture_error
        );
        if (masked != INVALID_TEXTURE) material.diffuse_texture = masked;
    }

    if (!diffuse_error.empty()) {
        if (error) *error = diffuse_error;
        return false;
    }

    return true;
}

} // namespace Models