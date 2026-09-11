#include "Models/Models.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Formats/Registry.hpp"

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

    std::vector<NodeData> nodes;
    std::vector<SceneData> scenes;
    std::uint32_t default_scene = INVALID_INDEX;
    std::vector<SkinData> skins;
    std::vector<CameraData> cameras;
    std::vector<LightData> lights;
    std::vector<ModelAnimationData> model_animations;
    std::vector<MaterialVariantData> variants;
    std::vector<VariantMappingData> variant_mappings;
    std::vector<InstanceData> instances;
    std::string extras_json;
    std::unordered_map<std::string, std::string> extensions_json;
};

std::vector<Model>& models() { static std::vector<Model> values; return values; }
std::vector<MeshData>& meshes() { static std::vector<MeshData> values; return values; }
std::vector<MaterialData>& materials() { static std::vector<MaterialData> values; return values; }
std::unordered_map<std::string, ModelHandle>& cache() { static std::unordered_map<std::string, ModelHandle> values; return values; }
std::uint64_t& resourceRevisionStorage() { static std::uint64_t value = 1u; return value; }

void touchResources()
{
    std::uint64_t& value = resourceRevisionStorage();
    ++value;
    if (value == 0u) value = 1u;
}

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

ModelHandle storeModel(const std::string& key, Formats::Document document)
{
    Model model;
    model.path = key;
    model.parts.reserve(document.parts.size());

    for (Formats::Part& source_part : document.parts) {
        const MeshHandle mesh_handle = registerMesh(std::move(source_part.mesh));
        const MaterialHandle material_handle = registerMaterial(std::move(source_part.material));
        if (mesh_handle == INVALID_MESH || material_handle == INVALID_MATERIAL)
            return INVALID_MODEL;
        model.parts.push_back({mesh_handle, material_handle, source_part.node, source_part.primitive});
    }

    if (document.has_skeleton && !document.skeleton.bones.empty())
        model.skeleton = Animation::registerSkeleton(std::move(document.skeleton));

    model.animations.reserve(document.animations.size());
    for (Animation::AnimationClip& animation_asset : document.animations)
        model.animations.push_back(Animation::registerClip(std::move(animation_asset)));

    model.nodes = std::move(document.nodes);
    model.scenes = std::move(document.scenes);
    model.default_scene = document.default_scene;
    model.skins = std::move(document.skins);
    model.cameras = std::move(document.cameras);
    model.lights = std::move(document.lights);
    model.model_animations = std::move(document.model_animations);
    model.variants = std::move(document.variants);
    model.instances = std::move(document.instances);
    model.extras_json = std::move(document.extras_json);
    model.extensions_json = std::move(document.extensions_json);

    model.variant_mappings.reserve(document.variant_materials.size());
    for (Formats::VariantMaterial& source : document.variant_materials) {
        const MaterialHandle handle = registerMaterial(std::move(source.material));
        if (handle == INVALID_MATERIAL) return INVALID_MODEL;
        model.variant_mappings.push_back({source.part, handle, std::move(source.variants)});
    }

    if (models().size() >= static_cast<std::size_t>(INVALID_MODEL)) return INVALID_MODEL;
    const ModelHandle handle = static_cast<ModelHandle>(models().size());
    models().push_back(std::move(model));
    cache().emplace(key, handle);
    return handle;
}

const Model *modelFor(ModelHandle handle)
{
    return handle < models().size() ? &models()[handle] : nullptr;
}

} // namespace

ModelHandle load(const std::string& path, std::string *error)
{
    if (error) error->clear();
    const std::string key = normalizedPath(path);
    if (const auto found = cache().find(key); found != cache().end()) return found->second;

    const std::string extension = lowerExtension(key);
    const Formats::Loader loader = Formats::loaderFor(extension);
    if (!loader) {
        if (error) *error = "unsupported model format: " + extension;
        return INVALID_MODEL;
    }

    Formats::Document document;
    if (!loader(key, &document, error)) return INVALID_MODEL;
    const bool empty_asset = document.parts.empty() && document.nodes.empty() && document.scenes.empty() &&
        document.cameras.empty() && document.lights.empty() && document.skins.empty() &&
        document.model_animations.empty() && document.instances.empty() &&
        document.extras_json.empty() && document.extensions_json.empty();
    if (empty_asset) {
        if (error) *error = "model contains no loadable asset data: " + key;
        return INVALID_MODEL;
    }
    return storeModel(key, std::move(document));
}

MeshHandle registerMesh(MeshData mesh)
{
    if (meshes().size() >= static_cast<std::size_t>(INVALID_MESH)) return INVALID_MESH;
    const MeshHandle handle = static_cast<MeshHandle>(meshes().size());
    meshes().push_back(std::move(mesh));
    touchResources();
    return handle;
}

MaterialHandle registerMaterial(MaterialData material)
{
    if (materials().size() >= static_cast<std::size_t>(INVALID_MATERIAL)) return INVALID_MATERIAL;
    const MaterialHandle handle = static_cast<MaterialHandle>(materials().size());
    materials().push_back(std::move(material));
    touchResources();
    return handle;
}

const ModelPart *part(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->parts.size() ? &model->parts[index] : nullptr;
}

const MeshData *mesh(MeshHandle handle)
{
    return handle < meshes().size() ? &meshes()[handle] : nullptr;
}

const MaterialData *material(MaterialHandle handle)
{
    return handle < materials().size() ? &materials()[handle] : nullptr;
}

bool updateMaterial(MaterialHandle handle, const MaterialData& replacement)
{
    if (handle >= materials().size()) return false;
    materials()[handle] = replacement;
    touchResources();
    return true;
}

std::size_t partCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->parts.size() : 0u;
}

std::size_t nodeCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->nodes.size() : 0u;
}

const NodeData *node(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->nodes.size() ? &model->nodes[index] : nullptr;
}

std::size_t sceneCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->scenes.size() : 0u;
}

const SceneData *scene(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->scenes.size() ? &model->scenes[index] : nullptr;
}

std::uint32_t defaultScene(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->default_scene : INVALID_INDEX;
}

std::size_t skinCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->skins.size() : 0u;
}

const SkinData *skin(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->skins.size() ? &model->skins[index] : nullptr;
}

std::size_t cameraCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->cameras.size() : 0u;
}

const CameraData *camera(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->cameras.size() ? &model->cameras[index] : nullptr;
}

std::size_t lightCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->lights.size() : 0u;
}

const LightData *light(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->lights.size() ? &model->lights[index] : nullptr;
}

std::size_t modelAnimationCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->model_animations.size() : 0u;
}

const ModelAnimationData *modelAnimation(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->model_animations.size() ? &model->model_animations[index] : nullptr;
}

std::size_t variantCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->variants.size() : 0u;
}

const MaterialVariantData *variant(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->variants.size() ? &model->variants[index] : nullptr;
}

std::size_t variantMappingCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->variant_mappings.size() : 0u;
}

const VariantMappingData *variantMapping(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->variant_mappings.size() ? &model->variant_mappings[index] : nullptr;
}

std::size_t instanceCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->instances.size() : 0u;
}

const InstanceData *instance(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->instances.size() ? &model->instances[index] : nullptr;
}

const std::string *modelExtras(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? &model->extras_json : nullptr;
}

const std::unordered_map<std::string, std::string> *modelExtensions(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? &model->extensions_json : nullptr;
}

Animation::SkeletonHandle skeleton(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->skeleton : Animation::INVALID_SKELETON;
}

std::size_t animationCount(ModelHandle handle)
{
    const Model *model = modelFor(handle);
    return model ? model->animations.size() : 0u;
}

Animation::ClipHandle animation(ModelHandle handle, std::size_t index)
{
    const Model *model = modelFor(handle);
    return model && index < model->animations.size()
        ? model->animations[index]
        : Animation::INVALID_CLIP;
}

Animation::ClipHandle animation(ModelHandle handle, std::string_view name)
{
    const Model *model = modelFor(handle);
    if (!model) return Animation::INVALID_CLIP;

    for (const Animation::ClipHandle candidate : model->animations) {
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

std::uint64_t resourceRevision()
{
    return resourceRevisionStorage();
}

void clearCache()
{
    cache().clear();
    models().clear();
    meshes().clear();
    materials().clear();
    clearTextureCache();
    Animation::clearAssets();
    touchResources();
}

} // namespace Models
