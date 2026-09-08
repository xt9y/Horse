#include "Models/Formats/FbxMaterial.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
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

bool diffuseProperty(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value.find("diffuse") != std::string::npos ||
        value.find("basecolor") != std::string::npos ||
        value.find("base_color") != std::string::npos ||
        value.find("albedo") != std::string::npos;
}

std::vector<ObjectId> connectedByKind(const Scene& scene, ObjectId id, const char *kind)
{
    return connectedObjects(scene, id, kind);
}

std::vector<ObjectId> materialTextures(const Scene& scene, ObjectId material)
{
    std::vector<ObjectId> preferred;
    std::vector<ObjectId> fallback;
    std::unordered_set<ObjectId> seen;
    auto inspect = [&](const std::vector<std::size_t>& indexes, bool incoming) {
        for (std::size_t index : indexes) {
            const Connection& connection = scene.connections[index];
            const ObjectId candidate_id = incoming ? connection.source : connection.destination;
            const Object *candidate = object(scene, candidate_id);
            if (!candidate || candidate->kind != "Texture" || !seen.insert(candidate_id).second) continue;
            if (diffuseProperty(connection.property)) preferred.push_back(candidate_id);
            else fallback.push_back(candidate_id);
        }
    };
    if (const auto found=scene.incoming.find(material); found!=scene.incoming.end()) inspect(found->second,true);
    if (const auto found=scene.outgoing.find(material); found!=scene.outgoing.end()) inspect(found->second,false);
    preferred.insert(preferred.end(),fallback.begin(),fallback.end());
    return preferred;
}

const FbxDocument::Bytes *contentBytes(const Object *object_value)
{
    if (!object_value) return nullptr;
    const auto *content = child(object_value->node,"Content");
    if (!content || content->properties.empty()) return nullptr;
    return content->properties[0].asBytes();
}

void appendFilename(const Object *value, std::vector<std::string> *out, std::unordered_set<std::string> *seen)
{
    if (!value || !out || !seen) return;
    const char *names[] = {"RelativeFilename","Filename","FileName"};
    for (const char *name : names) {
        const auto *node=child(value->node,name);
        if (!node || node->properties.empty()) continue;
        std::string filename=node->properties[0].asString();
        if (filename.empty()) continue;
        std::replace(filename.begin(),filename.end(),'\\','/');
        if (seen->insert(filename).second) out->push_back(std::move(filename));
    }
}

std::vector<std::filesystem::path> pathCandidates(
    const std::filesystem::path& source_path,
    const std::vector<std::string>& filenames)
{
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    auto append=[&](std::filesystem::path value) {
        value=value.lexically_normal();
        const std::string key=value.string();
        if (!key.empty() && seen.insert(key).second) result.push_back(std::move(value));
    };
    for (const std::string& filename : filenames) {
        std::filesystem::path path(filename);
        if (path.is_absolute()) append(path);
        else append(source_path.parent_path()/path);
        append(source_path.parent_path()/path.filename());
        append(source_path.parent_path()/"Textures"/path.filename());
        append(source_path.parent_path()/"textures"/path.filename());
    }
    return result;
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
    if (!out) return fail(error,"null FBX material destination");
    *out={};
    if (material_id==0) return true;
    const Object *material=object(scene,material_id);
    if (!material || material->kind!="Material") {
        return fail(error,"FBX material id "+std::to_string(material_id)+" does not resolve to a Material object");
    }

    out->name=material->name;
    const Animation::Vec3 diffuse=propertyVec3(*material,"DiffuseColor",{1.0f,1.0f,1.0f});
    out->color={diffuse.x,diffuse.y,diffuse.z};
    const double transparency=std::clamp(propertyScalar(*material,"TransparencyFactor",0.0),0.0,1.0);
    const double opacity=propertyScalar(*material,"Opacity",1.0-transparency);
    out->opacity=std::clamp(static_cast<float>(opacity),0.0f,1.0f);

    const auto textures=materialTextures(scene,material_id);
    if (textures.empty()) return true;

    std::string first_decode_error;
    for (ObjectId texture_id : textures) {
        const Object *texture_object=object(scene,texture_id);
        if (!texture_object) continue;
        std::vector<ObjectId> videos=connectedByKind(scene,texture_id,"Video");

        if (const auto *bytes=contentBytes(texture_object); bytes && !bytes->empty()) {
            const std::string key=source_path.string()+"#texture:"+std::to_string(texture_id)+"#embedded";
            std::string decode_error;
            const TextureHandle handle=loadTextureMemory(key,bytes->data(),bytes->size(),&decode_error);
            if (handle!=INVALID_TEXTURE) {
                out->diffuse_texture=handle;
                out->texture_path=key;
                return true;
            }
            if (first_decode_error.empty()) first_decode_error=decode_error.empty()?"cannot decode embedded FBX texture":decode_error;
        }
        for (ObjectId video_id : videos) {
            const Object *video=object(scene,video_id);
            if (const auto *bytes=contentBytes(video); bytes && !bytes->empty()) {
                const std::string key=source_path.string()+"#video:"+std::to_string(video_id)+"#embedded";
                std::string decode_error;
                const TextureHandle handle=loadTextureMemory(key,bytes->data(),bytes->size(),&decode_error);
                if (handle!=INVALID_TEXTURE) {
                    out->diffuse_texture=handle;
                    out->texture_path=key;
                    return true;
                }
                if (first_decode_error.empty()) first_decode_error=decode_error.empty()?"cannot decode embedded FBX video texture":decode_error;
            }
        }

        std::vector<std::string> filenames;
        std::unordered_set<std::string> seen_filenames;
        appendFilename(texture_object,&filenames,&seen_filenames);
        for (ObjectId video_id : videos) appendFilename(object(scene,video_id),&filenames,&seen_filenames);
        const auto candidates=pathCandidates(source_path,filenames);
        bool existing_candidate=false;
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!std::filesystem::exists(candidate,ec) || ec) continue;
            existing_candidate=true;
            std::string decode_error;
            const TextureHandle handle=loadTexture(candidate.string(),&decode_error);
            if (handle!=INVALID_TEXTURE) {
                out->diffuse_texture=handle;
                out->texture_path=candidate.string();
                return true;
            }
            if (first_decode_error.empty()) first_decode_error=decode_error.empty()?"cannot decode image":decode_error;
        }
        if (existing_candidate && !first_decode_error.empty()) {
            return fail(error,"FBX material `"+material->name+"` references an image Horse cannot decode: "+first_decode_error);
        }
    }

    // A filename reference whose file is absent stays unresolved rather than
    // fabricating a texture. The source material and reference path remain
    // valid metadata even when the external package is incomplete.
    return true;
}

} // namespace Models::FbxInternal
