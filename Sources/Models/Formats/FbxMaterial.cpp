#include "Models/Formats/FbxMaterial.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace Models::FbxInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

enum class TextureRole {
    BaseColor,
    Normal,
    Roughness,
    Metallic,
    AmbientOcclusion,
    Emissive,
    Opacity,
    Unknown,
};

struct MaterialTexture {
    ObjectId id = 0;
    TextureRole role = TextureRole::Unknown;
};

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

TextureRole textureRole(const std::string& property)
{
    const std::string value = lower(property);
    if (value.find("normal") != std::string::npos || value.find("bump") != std::string::npos)
        return TextureRole::Normal;
    if (value.find("rough") != std::string::npos)
        return TextureRole::Roughness;
    if (value.find("metal") != std::string::npos)
        return TextureRole::Metallic;
    if (value.find("ambientocclusion") != std::string::npos ||
        value.find("ambient_occlusion") != std::string::npos || value == "ao")
        return TextureRole::AmbientOcclusion;
    if (value.find("emiss") != std::string::npos)
        return TextureRole::Emissive;
    if (value.find("opacity") != std::string::npos ||
        value.find("transparen") != std::string::npos || value.find("alpha") != std::string::npos)
        return TextureRole::Opacity;
    if (value.find("diffuse") != std::string::npos ||
        value.find("basecolor") != std::string::npos ||
        value.find("base_color") != std::string::npos ||
        value.find("albedo") != std::string::npos)
        return TextureRole::BaseColor;
    return TextureRole::Unknown;
}

std::vector<ObjectId> connectedByKind(const Scene& scene, ObjectId id, const char *kind)
{
    return connectedObjects(scene, id, kind);
}

std::vector<MaterialTexture> materialTextures(const Scene& scene, ObjectId material)
{
    std::vector<MaterialTexture> result;
    std::unordered_set<ObjectId> seen;
    bool has_base_color = false;

    auto inspect = [&](const std::vector<std::size_t>& indexes, bool incoming) {
        for (std::size_t index : indexes) {
            const Connection& connection = scene.connections[index];
            const ObjectId candidate_id = incoming ? connection.source : connection.destination;
            const Object *candidate = object(scene, candidate_id);
            if (!candidate || candidate->kind != "Texture" || !seen.insert(candidate_id).second) continue;
            const TextureRole role = textureRole(connection.property);
            if (role == TextureRole::BaseColor) has_base_color = true;
            result.push_back({candidate_id, role});
        }
    };

    if (const auto found = scene.incoming.find(material); found != scene.incoming.end())
        inspect(found->second, true);
    if (const auto found = scene.outgoing.find(material); found != scene.outgoing.end())
        inspect(found->second, false);

    if (!has_base_color) {
        for (MaterialTexture& texture : result) {
            if (texture.role != TextureRole::Unknown) continue;
            texture.role = TextureRole::BaseColor;
            break;
        }
    }
    return result;
}

const FbxDocument::Bytes *contentBytes(const Object *object_value)
{
    if (!object_value) return nullptr;
    const auto *content = child(object_value->node, "Content");
    if (!content || content->properties.empty()) return nullptr;
    return content->properties[0].asBytes();
}

void appendFilename(
    const Object *value,
    std::vector<std::string> *out,
    std::unordered_set<std::string> *seen)
{
    if (!value || !out || !seen) return;
    const char *names[] = {"RelativeFilename", "Filename", "FileName"};
    for (const char *name : names) {
        const auto *node = child(value->node, name);
        if (!node || node->properties.empty()) continue;
        std::string filename = node->properties[0].asString();
        if (filename.empty()) continue;
        std::replace(filename.begin(), filename.end(), '\\', '/');
        if (seen->insert(filename).second) out->push_back(std::move(filename));
    }
}

std::vector<std::filesystem::path> pathCandidates(
    const std::filesystem::path& source_path,
    const std::vector<std::string>& filenames)
{
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    auto append = [&](std::filesystem::path value) {
        value = value.lexically_normal();
        const std::string key = value.string();
        if (!key.empty() && seen.insert(key).second) result.push_back(std::move(value));
    };
    for (const std::string& filename : filenames) {
        std::filesystem::path path(filename);
        if (path.is_absolute()) append(path);
        else append(source_path.parent_path() / path);
        append(source_path.parent_path() / path.filename());
        append(source_path.parent_path() / "Textures" / path.filename());
        append(source_path.parent_path() / "textures" / path.filename());
    }
    return result;
}

TextureHandle loadFbxTexture(
    const Scene& scene,
    ObjectId texture_id,
    const std::filesystem::path& source_path,
    std::string *resolved_path,
    std::string *decode_error)
{
    if (resolved_path) resolved_path->clear();
    const Object *texture_object = object(scene, texture_id);
    if (!texture_object) return INVALID_TEXTURE;
    const std::vector<ObjectId> videos = connectedByKind(scene, texture_id, "Video");

    auto decodeEmbedded = [&](const FbxDocument::Bytes *bytes, const std::string& key) -> TextureHandle {
        if (!bytes || bytes->empty()) return INVALID_TEXTURE;
        std::string error;
        const TextureHandle handle = loadTextureMemory(key, bytes->data(), bytes->size(), &error);
        if (handle != INVALID_TEXTURE) {
            if (resolved_path) *resolved_path = key;
            return handle;
        }
        if (decode_error && decode_error->empty())
            *decode_error = error.empty() ? "cannot decode embedded FBX texture" : error;
        return INVALID_TEXTURE;
    };

    if (const TextureHandle handle = decodeEmbedded(
            contentBytes(texture_object),
            source_path.string() + "#texture:" + std::to_string(texture_id) + "#embedded");
        handle != INVALID_TEXTURE)
    {
        return handle;
    }

    for (ObjectId video_id : videos) {
        if (const TextureHandle handle = decodeEmbedded(
                contentBytes(object(scene, video_id)),
                source_path.string() + "#video:" + std::to_string(video_id) + "#embedded");
            handle != INVALID_TEXTURE)
        {
            return handle;
        }
    }

    std::vector<std::string> filenames;
    std::unordered_set<std::string> seen_filenames;
    appendFilename(texture_object, &filenames, &seen_filenames);
    for (ObjectId video_id : videos)
        appendFilename(object(scene, video_id), &filenames, &seen_filenames);

    bool existing_candidate = false;
    for (const auto& candidate : pathCandidates(source_path, filenames)) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec) continue;
        existing_candidate = true;
        std::string error;
        const TextureHandle handle = loadTexture(candidate.string(), &error);
        if (handle != INVALID_TEXTURE) {
            if (resolved_path) *resolved_path = candidate.string();
            return handle;
        }
        if (decode_error && decode_error->empty())
            *decode_error = error.empty() ? "cannot decode image" : error;
    }

    if (!existing_candidate && decode_error) decode_error->clear();
    return INVALID_TEXTURE;
}

void assignTexture(MaterialData *material, TextureRole role, TextureHandle handle, std::string path)
{
    if (!material || handle == INVALID_TEXTURE) return;
    switch (role) {
    case TextureRole::BaseColor:
        material->diffuse_texture = handle;
        material->texture_path = std::move(path);
        break;
    case TextureRole::Normal:
        material->normal_texture = handle;
        material->normal_texture_path = std::move(path);
        break;
    case TextureRole::Roughness:
        material->roughness_texture = handle;
        material->roughness_texture_path = std::move(path);
        break;
    case TextureRole::Metallic:
        material->metallic_texture = handle;
        material->metallic_texture_path = std::move(path);
        break;
    case TextureRole::AmbientOcclusion:
        material->ambient_occlusion_texture = handle;
        material->ambient_occlusion_texture_path = std::move(path);
        break;
    case TextureRole::Emissive:
        material->emissive_texture = handle;
        material->emissive_texture_path = std::move(path);
        if (material->emissive_strength <= 0.0f) material->emissive_strength = 1.0f;
        break;
    case TextureRole::Opacity:
        material->opacity_texture = handle;
        material->opacity_texture_path = std::move(path);
        break;
    case TextureRole::Unknown:
        break;
    }
}

} // namespace

bool convertMaterial(
    const Scene& scene,
    ObjectId material_id,
    const std::filesystem::path& source_path,
    MaterialData *out,
    std::string *error)
{
    if (error) error->clear();
    if (!out) return fail(error, "null FBX material destination");
    *out = {};
    if (material_id == 0) return true;

    const Object *material = object(scene, material_id);
    if (!material || material->kind != "Material") {
        return fail(
            error,
            "FBX material id " + std::to_string(material_id) + " does not resolve to a Material object"
        );
    }

    out->name = material->name;
    const Animation::Vec3 diffuse = propertyVec3(*material, "DiffuseColor", {1.0f, 1.0f, 1.0f});
    out->color = {diffuse.x, diffuse.y, diffuse.z};

    const double transparency = std::clamp(
        propertyScalar(*material, "TransparencyFactor", 0.0), 0.0, 1.0);
    const double opacity = propertyScalar(*material, "Opacity", 1.0 - transparency);
    out->opacity = std::clamp(static_cast<float>(opacity), 0.0f, 1.0f);

    out->roughness = std::clamp(
        static_cast<float>(propertyScalar(*material, "Roughness", out->roughness)), 0.0f, 1.0f);
    const double metallic = propertyScalar(
        *material,
        "Metalness",
        propertyScalar(*material, "Metallic", out->metallic)
    );
    out->metallic = std::clamp(static_cast<float>(metallic), 0.0f, 1.0f);
    out->ambient_occlusion = std::clamp(
        static_cast<float>(propertyScalar(*material, "AmbientOcclusion", out->ambient_occlusion)),
        0.0f,
        1.0f
    );

    const Animation::Vec3 emissive = propertyVec3(*material, "EmissiveColor", {0.0f, 0.0f, 0.0f});
    out->emissive_color = {emissive.x, emissive.y, emissive.z};
    const float emissive_peak = std::max({
        out->emissive_color.x,
        out->emissive_color.y,
        out->emissive_color.z,
    });
    out->emissive_strength = std::max(
        static_cast<float>(propertyScalar(*material, "EmissiveFactor", emissive_peak > 0.0f ? 1.0 : 0.0)),
        0.0f
    );

    out->ior = std::max(
        static_cast<float>(propertyScalar(
            *material,
            "IndexOfRefraction",
            propertyScalar(*material, "IOR", out->ior)
        )),
        1.0f
    );
    out->clearcoat = std::clamp(
        static_cast<float>(propertyScalar(*material, "CoatWeight", out->clearcoat)), 0.0f, 1.0f);
    out->clearcoat_roughness = std::clamp(
        static_cast<float>(propertyScalar(
            *material,
            "CoatRoughness",
            out->clearcoat_roughness
        )),
        0.0f,
        1.0f
    );

    for (const MaterialTexture& texture : materialTextures(scene, material_id)) {
        if (texture.role == TextureRole::Unknown) continue;
        std::string resolved_path;
        std::string decode_error;
        const TextureHandle handle = loadFbxTexture(
            scene,
            texture.id,
            source_path,
            &resolved_path,
            &decode_error
        );
        if (handle != INVALID_TEXTURE) {
            assignTexture(out, texture.role, handle, std::move(resolved_path));
            continue;
        }
        if (!decode_error.empty()) {
            return fail(
                error,
                "FBX material `" + material->name + "` references an image Horse cannot decode: " +
                    decode_error
            );
        }
    }

    return true;
}

} // namespace Models::FbxInternal
