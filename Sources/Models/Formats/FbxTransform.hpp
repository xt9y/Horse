#ifndef RW_ENGINE_MODELS_FORMATS_FBX_TRANSFORM_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_TRANSFORM_HPP

#include "Models/Formats/FbxScene.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Models::FbxInternal {

enum class RotationOrder { XYZ, XZY, YZX, YXZ, ZXY, ZYX };

struct TransformProperties {
    Animation::Vec3 translation{};
    Animation::Vec3 rotation_degrees{};
    Animation::Vec3 scale{1.0f, 1.0f, 1.0f};
    Animation::Vec3 pre_rotation{};
    Animation::Vec3 post_rotation{};
    Animation::Vec3 rotation_offset{};
    Animation::Vec3 rotation_pivot{};
    Animation::Vec3 scaling_offset{};
    Animation::Vec3 scaling_pivot{};
    Animation::Vec3 geometric_translation{};
    Animation::Vec3 geometric_rotation{};
    Animation::Vec3 geometric_scale{1.0f, 1.0f, 1.0f};
    RotationOrder rotation_order = RotationOrder::XYZ;
    int inherit_type = 0;
};

bool readTransformProperties(const Object& object, TransformProperties *out, std::string *error);
bool localModelMatrix(const TransformProperties& properties, Animation::Mat4 *out, std::string *error);
bool geometricMatrix(const TransformProperties& properties, Animation::Mat4 *out, std::string *error);
bool globalModelMatrix(const Scene& scene, ObjectId model, Animation::Mat4 *out, std::string *error);
bool globalModelMatrix(
    const Scene& scene,
    ObjectId model,
    const std::unordered_map<ObjectId, TransformProperties>& overrides,
    Animation::Mat4 *out,
    std::string *error);

Animation::Mat4 matrixFromFbxArray(const std::vector<double>& values);
bool invertMatrix(const Animation::Mat4& matrix, Animation::Mat4 *out);
float linearDeterminant(const Animation::Mat4& matrix);
bool decomposeTrs(
    const Animation::Mat4& matrix,
    Animation::Transform *out,
    std::string *error,
    const std::string& context);

} // namespace Models::FbxInternal

#endif
