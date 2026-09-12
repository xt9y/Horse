#ifndef RW_ENGINE_MODELS_HPP
#define RW_ENGINE_MODELS_HPP

#include "Animation/Animation.hpp"
#include "Models/Core/Material.hpp"
#include "Models/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Models {

using ModelHandle = std::uint32_t;
using MeshHandle = std::uint32_t;
using MaterialHandle = std::uint32_t;

constexpr ModelHandle INVALID_MODEL = UINT32_MAX;
constexpr MeshHandle INVALID_MESH = UINT32_MAX;
constexpr MaterialHandle INVALID_MATERIAL = UINT32_MAX;
constexpr std::uint32_t INVALID_INDEX = UINT32_MAX;

enum class PrimitiveMode : std::uint8_t {
    Points = 0,
    Lines = 1,
    LineLoop = 2,
    LineStrip = 3,
    Triangles = 4,
    TriangleStrip = 5,
    TriangleFan = 6,
};

struct Joint4 {
    std::uint16_t x = 0u;
    std::uint16_t y = 0u;
    std::uint16_t z = 0u;
    std::uint16_t w = 0u;
};

struct AttributeData {
    int component_type = 0;
    std::uint32_t components = 0u;
    bool normalized = false;
    std::vector<double> values;

    AttributeData() = default;
    explicit AttributeData(std::vector<float> source)
    {
        values.reserve(source.size());
        for (const float value : source) values.push_back(static_cast<double>(value));
    }
};

struct Vertex {
    Vec3 position;
    Vec3 normal {0.0f, 0.0f, 1.0f};
    Vec2 uv;
    Animation::SkinWeights skin;
    Vec4 tangent {1.0f, 0.0f, 0.0f, 1.0f};
    Vec4 color {1.0f, 1.0f, 1.0f, 1.0f};
};

struct Bounds {
    Vec3 minimum;
    Vec3 maximum;
};

struct MorphTargetData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec3> tangents;
    std::unordered_map<std::string, AttributeData> attributes;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Bounds bounds;
    std::vector<Animation::Mat4> skin_inverse_bind;

    PrimitiveMode primitive_mode = PrimitiveMode::Triangles;
    std::vector<std::uint32_t> source_indices;
    std::vector<std::vector<Vec2>> texcoord_sets;
    std::vector<std::vector<Vec4>> color_sets;
    std::vector<std::vector<Joint4>> joint_sets;
    std::vector<std::vector<Vec4>> weight_sets;
    std::unordered_map<std::string, AttributeData> attributes;
    std::vector<MorphTargetData> morph_targets;
    std::vector<float> morph_weights;
    std::vector<std::string> morph_names;
    std::string extras_json;
    std::unordered_map<std::string, std::string> extensions_json;
};

struct ModelPart {
    MeshHandle mesh = INVALID_MESH;
    MaterialHandle material = INVALID_MATERIAL;
    std::uint32_t node = INVALID_INDEX;
    std::uint32_t primitive = 0u;
    std::uint32_t source_material = INVALID_INDEX;
};

struct NodeData {
    std::string name;
    std::int32_t parent = -1;
    std::vector<std::uint32_t> children;
    std::vector<std::uint32_t> parts;
    std::uint32_t mesh = INVALID_INDEX;
    std::uint32_t skin = INVALID_INDEX;
    std::uint32_t camera = INVALID_INDEX;
    std::uint32_t light = INVALID_INDEX;
    Vec3 translation{};
    Quat rotation{};
    Vec3 scale {1.0f, 1.0f, 1.0f};
    Mat4 matrix = identityMatrix();
    bool has_matrix = false;
    bool visible = true;
    bool selectable = true;
    bool hoverable = true;
    std::vector<float> weights;
    std::string extras_json;
    std::unordered_map<std::string, std::string> extensions_json;
};

struct SceneData {
    std::string name;
    std::vector<std::uint32_t> nodes;
    std::string extras_json;
    std::unordered_map<std::string, std::string> extensions_json;
};

struct SkinData {
    std::string name;
    std::uint32_t skeleton = INVALID_INDEX;
    std::vector<std::uint32_t> joints;
    std::vector<Mat4> inverse_bind_matrices;
};

enum class CameraType : std::uint8_t {
    Perspective,
    Orthographic,
};

struct CameraData {
    std::string name;
    CameraType type = CameraType::Perspective;
    float yfov = 0.78539816339f;
    float aspect_ratio = 0.0f;
    float znear = 0.1f;
    float zfar = 0.0f;
    float xmag = 1.0f;
    float ymag = 1.0f;
};

enum class AssetLightType : std::uint8_t {
    Directional,
    Point,
    Spot,
};

struct LightData {
    std::string name;
    AssetLightType type = AssetLightType::Point;
    Vec3 color {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 0.0f;
    float inner_cone_angle = 0.0f;
    float outer_cone_angle = 0.78539816339f;
    std::string ies_uri;
};

enum class AnimationInterpolation : std::uint8_t {
    Linear,
    Step,
    CubicSpline,
};

enum class AnimationPath : std::uint8_t {
    Translation,
    Rotation,
    Scale,
    Weights,
    Pointer,
};

struct AnimationSamplerData {
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> input;
    std::vector<float> output;
    std::uint32_t components = 0u;
};

struct AnimationChannelData {
    std::uint32_t sampler = INVALID_INDEX;
    std::uint32_t node = INVALID_INDEX;
    AnimationPath path = AnimationPath::Translation;
    std::string pointer;
};

struct ModelAnimationData {
    std::string name;
    std::vector<AnimationSamplerData> samplers;
    std::vector<AnimationChannelData> channels;
    float duration = 0.0f;
};

struct MaterialVariantData {
    std::string name;
};

struct VariantMappingData {
    std::uint32_t part = INVALID_INDEX;
    MaterialHandle material = INVALID_MATERIAL;
    std::vector<std::uint32_t> variants;
    std::uint32_t source_material = INVALID_INDEX;
};

struct InstanceData {
    std::uint32_t node = INVALID_INDEX;
    std::vector<Vec3> translations;
    std::vector<Quat> rotations;
    std::vector<Vec3> scales;
    std::unordered_map<std::string, AttributeData> attributes;
};

ModelHandle load(const std::string& path, std::string *error = nullptr);
MeshHandle registerMesh(MeshData mesh);
MaterialHandle registerMaterial(MaterialData material);
const ModelPart *part(ModelHandle model, std::size_t index);
const MeshData *mesh(MeshHandle handle);
bool updateMesh(MeshHandle handle, const MeshData& replacement);
const MaterialData *material(MaterialHandle handle);
bool updateMaterial(MaterialHandle handle, const MaterialData& replacement);
std::size_t partCount(ModelHandle model);

std::size_t nodeCount(ModelHandle model);
const NodeData *node(ModelHandle model, std::size_t index);
std::size_t sceneCount(ModelHandle model);
const SceneData *scene(ModelHandle model, std::size_t index);
std::uint32_t defaultScene(ModelHandle model);
std::size_t skinCount(ModelHandle model);
const SkinData *skin(ModelHandle model, std::size_t index);
std::size_t cameraCount(ModelHandle model);
const CameraData *camera(ModelHandle model, std::size_t index);
std::size_t lightCount(ModelHandle model);
const LightData *light(ModelHandle model, std::size_t index);
std::size_t modelAnimationCount(ModelHandle model);
const ModelAnimationData *modelAnimation(ModelHandle model, std::size_t index);
std::size_t variantCount(ModelHandle model);
const MaterialVariantData *variant(ModelHandle model, std::size_t index);
std::size_t variantMappingCount(ModelHandle model);
const VariantMappingData *variantMapping(ModelHandle model, std::size_t index);
std::size_t instanceCount(ModelHandle model);
const InstanceData *instance(ModelHandle model, std::size_t index);
const std::string *modelExtras(ModelHandle model);
const std::unordered_map<std::string, std::string> *modelExtensions(ModelHandle model);

Animation::SkeletonHandle skeleton(ModelHandle model);
std::size_t animationCount(ModelHandle model);
Animation::ClipHandle animation(ModelHandle model, std::size_t index);
Animation::ClipHandle animation(ModelHandle model, std::string_view name);
bool animated(ModelHandle model);

std::uint64_t resourceRevision();
void clearCache();

} // namespace Models

#endif