#ifndef RW_ENGINE_MODELS_FORMATS_FBX_SKIN_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_SKIN_HPP

#include "Models/Formats/FbxTransform.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Models::FbxInternal {

struct SkeletonBuild {
    std::vector<ObjectId> models;
    std::unordered_map<ObjectId, std::uint16_t> indices;
};

ObjectId skinForGeometry(const Scene& scene, ObjectId geometry);
SkeletonBuild collectSkeleton(const Scene& scene);

bool makeSkeleton(
    const Scene& scene,
    const SkeletonBuild& build,
    const std::string& name,
    Animation::Skeleton *out,
    std::string *error);

bool buildControlWeights(
    const Scene& scene,
    ObjectId skin,
    const SkeletonBuild& skeleton,
    std::size_t control_count,
    std::vector<Animation::SkinWeights> *out,
    std::string *error);

bool buildMeshBindPalette(
    const Scene& scene,
    ObjectId skin,
    const SkeletonBuild& skeleton,
    std::vector<Animation::Mat4> *out,
    std::string *error);

} // namespace Models::FbxInternal

#endif
