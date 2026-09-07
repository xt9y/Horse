#include "Models/Models.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Formats/Fbx.hpp"
#include "Models/Formats/Obj.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace Models {
namespace {

struct Model {
    std::string path;
    std::vector<ModelPart> parts;
    Animation::SkeletonHandle skeleton = Animation::INVALID_SKELETON;
    std::vector<Animation::ClipHandle> animations;
};

std::vector<Model>& models() { static std::vector<Model> values; return values; }
std::vector<MeshData>& meshes() { static std::vector<MeshData> values; return values; }
std::vector<MaterialData>& materials() { static std::vector<MaterialData> values; return values; }
std::unordered_map<std::string, ModelHandle>& cache() { static std::unordered_map<std::string, ModelHandle> values; return values; }

std::string normalizedPath(const std::string& path)
{
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? std::filesystem::path(path) : absolute).lexically_normal().string();
}

std::string lowerExtension(const std::string& path)
{
    std::string value = std::filesystem::path(path).extension().string();
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return value;
}

template <typename Parts>
ModelHandle storeModel(
    const std::string& key,
    Parts& source_parts,
    Animation::SkeletonHandle skeleton_handle,
    std::vector<Animation::ClipHandle> animation_handles)
{
    Model model;
    model.path = key;
    model.skeleton = skeleton_handle;
    model.animations = std::move(animation_handles);
    model.parts.reserve(source_parts.size());

    for (auto& source_part : source_parts) {
        const MeshHandle mesh_handle = static_cast<MeshHandle>(meshes().size());
        meshes().push_back(std::move(source_part.mesh));

        const MaterialHandle material_handle = static_cast<MaterialHandle>(materials().size());
        materials().push_back(std::move(source_part.material));
        model.parts.push_back({mesh_handle, material_handle});
    }

    const ModelHandle handle = static_cast<ModelHandle>(models().size());
    models().push_back(std::move(model));
    cache().emplace(key, handle);
    return handle;
}

} // namespace

ModelHandle load(const std::string& path, std::string *error)
{
    if (error) error->clear();
    const std::string key = normalizedPath(path);
    if (const auto found = cache().find(key); found != cache().end()) return found->second;

    const std::string extension = lowerExtension(key);
    if (extension == ".obj") {
        Obj::Document document;
        if (!Obj::load(key, &document, error)) return INVALID_MODEL;
        return storeModel(
            key,
            document.parts,
            Animation::INVALID_SKELETON,
            {}
        );
    }

    if (extension == ".fbx") {
        Fbx::Document document;
        if (!Fbx::load(key, &document, error)) return INVALID_MODEL;

        Animation::SkeletonHandle skeleton_handle = Animation::INVALID_SKELETON;
        if (document.has_skeleton && !document.skeleton.bones.empty()) {
            skeleton_handle = Animation::registerSkeleton(std::move(document.skeleton));
        }

        std::vector<Animation::ClipHandle> animation_handles;
        animation_handles.reserve(document.animations.size());
        for (Animation::AnimationClip& animation_asset : document.animations) {
            animation_handles.push_back(Animation::registerClip(std::move(animation_asset)));
        }

        return storeModel(
            key,
            document.parts,
            skeleton_handle,
            std::move(animation_handles)
        );
    }

    if (error) *error = "unsupported model format: " + extension;
    return INVALID_MODEL;
}

const ModelPart *part(ModelHandle handle, std::size_t index)
{
    if (handle >= models().size()) return nullptr;
    const Model& model = models()[handle];
    return index < model.parts.size() ? &model.parts[index] : nullptr;
}

const MeshData *mesh(MeshHandle handle)
{
    return handle < meshes().size() ? &meshes()[handle] : nullptr;
}

const MaterialData *material(MaterialHandle handle)
{
    return handle < materials().size() ? &materials()[handle] : nullptr;
}

std::size_t partCount(ModelHandle handle)
{
    return handle < models().size() ? models()[handle].parts.size() : 0u;
}

Animation::SkeletonHandle skeleton(ModelHandle handle)
{
    return handle < models().size()
        ? models()[handle].skeleton
        : Animation::INVALID_SKELETON;
}

std::size_t animationCount(ModelHandle handle)
{
    return handle < models().size() ? models()[handle].animations.size() : 0u;
}

Animation::ClipHandle animation(ModelHandle handle, std::size_t index)
{
    if (handle >= models().size()) return Animation::INVALID_CLIP;
    const Model& model = models()[handle];
    return index < model.animations.size()
        ? model.animations[index]
        : Animation::INVALID_CLIP;
}

Animation::ClipHandle animation(ModelHandle handle, std::string_view name)
{
    if (handle >= models().size()) return Animation::INVALID_CLIP;

    for (const Animation::ClipHandle candidate : models()[handle].animations) {
        const Animation::AnimationClip *asset = Animation::clip(candidate);
        if (asset && asset->name == name) return candidate;
    }

    return Animation::INVALID_CLIP;
}

bool animated(ModelHandle handle)
{
    return skeleton(handle) != Animation::INVALID_SKELETON
        && animationCount(handle) != 0u;
}

void clearCache()
{
    cache().clear();
    models().clear();
    meshes().clear();
    materials().clear();
    clearTextureCache();
    Animation::clearAssets();
}

} // namespace Models
