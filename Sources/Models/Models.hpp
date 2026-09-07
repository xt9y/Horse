#ifndef RW_ENGINE_MODELS_HPP
#define RW_ENGINE_MODELS_HPP

#include "Animation/Animation.hpp"
#include "Models/Core/Material.hpp"
#include "Models/Core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Models {

using ModelHandle = std::uint32_t;
using MeshHandle = std::uint32_t;
using MaterialHandle = std::uint32_t;

constexpr ModelHandle INVALID_MODEL = UINT32_MAX;
constexpr MeshHandle INVALID_MESH = UINT32_MAX;
constexpr MaterialHandle INVALID_MATERIAL = UINT32_MAX;

struct Vertex {
    Vec3 position;
    Vec3 normal {0.0f, 0.0f, 1.0f};
    Vec2 uv;
    Animation::SkinWeights skin;
};

struct Bounds {
    Vec3 minimum;
    Vec3 maximum;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Bounds bounds;
};

struct ModelPart {
    MeshHandle mesh = INVALID_MESH;
    MaterialHandle material = INVALID_MATERIAL;
};

ModelHandle load(const std::string& path, std::string *error = nullptr);
const ModelPart *part(ModelHandle model, std::size_t index);
const MeshData *mesh(MeshHandle handle);
const MaterialData *material(MaterialHandle handle);
std::size_t partCount(ModelHandle model);

Animation::SkeletonHandle skeleton(ModelHandle model);
std::size_t animationCount(ModelHandle model);
Animation::ClipHandle animation(ModelHandle model, std::size_t index);
Animation::ClipHandle animation(ModelHandle model, std::string_view name);
bool animated(ModelHandle model);

void clearCache();

} // namespace Models

#endif
