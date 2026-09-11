#ifndef HORSE_MODELS_FORMATS_REGISTRY_HPP
#define HORSE_MODELS_FORMATS_REGISTRY_HPP

#include "Animation/Animation.hpp"
#include "Models/Models.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Models::Formats {

struct VariantMaterial {
    std::uint32_t part = INVALID_INDEX;
    MaterialData material;
    std::vector<std::uint32_t> variants;
};

struct Part {
    MeshData mesh;
    MaterialData material;
    std::uint32_t node = INVALID_INDEX;
    std::uint32_t primitive = 0u;
};

struct Document {
    std::vector<Part> parts;
    Animation::Skeleton skeleton;
    std::vector<Animation::AnimationClip> animations;
    bool has_skeleton = false;

    std::vector<NodeData> nodes;
    std::vector<SceneData> scenes;
    std::uint32_t default_scene = INVALID_INDEX;
    std::vector<SkinData> skins;
    std::vector<CameraData> cameras;
    std::vector<LightData> lights;
    std::vector<ModelAnimationData> model_animations;
    std::vector<MaterialVariantData> variants;
    std::vector<VariantMaterial> variant_materials;
    std::vector<InstanceData> instances;
    std::string extras_json;
    std::unordered_map<std::string, std::string> extensions_json;
};

using Loader = bool (*)(const std::string&, Document *, std::string *);

bool registerLoader(std::string extension, Loader loader);
Loader loaderFor(std::string_view extension);

class Registration {
public:
    Registration(const char *extension, Loader loader);
};

} // namespace Models::Formats

#endif
