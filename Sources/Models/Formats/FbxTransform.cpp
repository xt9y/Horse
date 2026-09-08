#include "Models/Formats/FbxTransform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Models::FbxInternal {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEpsilon = 1.0e-7f;

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

Animation::Mat4 identity()
{
    return {};
}

Animation::Mat4 translation(Animation::Vec3 value)
{
    Animation::Mat4 result;
    result.value[12] = value.x;
    result.value[13] = value.y;
    result.value[14] = value.z;
    return result;
}

Animation::Mat4 scaling(Animation::Vec3 value)
{
    Animation::Mat4 result;
    result.value[0] = value.x;
    result.value[5] = value.y;
    result.value[10] = value.z;
    return result;
}

Animation::Mat4 rotationX(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Animation::Mat4 result;
    result.value[5] = c;
    result.value[6] = s;
    result.value[9] = -s;
    result.value[10] = c;
    return result;
}

Animation::Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Animation::Mat4 result;
    result.value[0] = c;
    result.value[2] = -s;
    result.value[8] = s;
    result.value[10] = c;
    return result;
}

Animation::Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Animation::Mat4 result;
    result.value[0] = c;
    result.value[1] = s;
    result.value[4] = -s;
    result.value[5] = c;
    return result;
}

Animation::Mat4 axisRotation(char axis, Animation::Vec3 degrees)
{
    if (axis == 'X') return rotationX(degrees.x);
    if (axis == 'Y') return rotationY(degrees.y);
    return rotationZ(degrees.z);
}

Animation::Mat4 eulerMatrix(Animation::Vec3 degrees, RotationOrder order)
{
    std::array<char, 3> axes{};
    switch (order) {
        case RotationOrder::XYZ: axes = {'X', 'Y', 'Z'}; break;
        case RotationOrder::XZY: axes = {'X', 'Z', 'Y'}; break;
        case RotationOrder::YZX: axes = {'Y', 'Z', 'X'}; break;
        case RotationOrder::YXZ: axes = {'Y', 'X', 'Z'}; break;
        case RotationOrder::ZXY: axes = {'Z', 'X', 'Y'}; break;
        case RotationOrder::ZYX: axes = {'Z', 'Y', 'X'}; break;
    }
    Animation::Mat4 result = identity();
    for (char axis : axes) result = Animation::multiply(axisRotation(axis, degrees), result);
    return result;
}

Animation::Vec3 column(const Animation::Mat4& matrix, int index)
{
    return {
        matrix.value[index * 4 + 0],
        matrix.value[index * 4 + 1],
        matrix.value[index * 4 + 2],
    };
}

float dot(Animation::Vec3 a, Animation::Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Animation::Vec3 cross(Animation::Vec3 a, Animation::Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float length(Animation::Vec3 value)
{
    return std::sqrt(std::max(dot(value, value), 0.0f));
}

Animation::Vec3 mul(Animation::Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Animation::Vec3 sub(Animation::Vec3 a, Animation::Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Animation::Vec3 normalize(Animation::Vec3 value)
{
    const float size = length(value);
    return size > kEpsilon ? mul(value, 1.0f / size) : Animation::Vec3{};
}

Animation::Mat4 rotationOnly(const Animation::Mat4& matrix)
{
    Animation::Vec3 x = column(matrix, 0);
    Animation::Vec3 y = column(matrix, 1);
    Animation::Vec3 z = column(matrix, 2);

    x = normalize(x);
    y = normalize(sub(y, mul(x, dot(x, y))));
    Animation::Vec3 candidate_z = normalize(sub(sub(z, mul(x, dot(x, z))), mul(y, dot(y, z))));
    if (length(candidate_z) <= kEpsilon) candidate_z = normalize(cross(x, y));
    if (dot(cross(x, y), candidate_z) < 0.0f) candidate_z = mul(candidate_z, -1.0f);

    Animation::Mat4 result;
    result.value[0] = x.x; result.value[1] = x.y; result.value[2] = x.z;
    result.value[4] = y.x; result.value[5] = y.y; result.value[6] = y.z;
    result.value[8] = candidate_z.x; result.value[9] = candidate_z.y; result.value[10] = candidate_z.z;
    return result;
}

Animation::Quat quatFromRotation(const Animation::Mat4& matrix)
{
    const float m00 = matrix.value[0];
    const float m11 = matrix.value[5];
    const float m22 = matrix.value[10];
    const float trace = m00 + m11 + m22;
    Animation::Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (matrix.value[6] - matrix.value[9]) / s;
        q.y = (matrix.value[8] - matrix.value[2]) / s;
        q.z = (matrix.value[1] - matrix.value[4]) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (matrix.value[6] - matrix.value[9]) / s;
        q.x = 0.25f * s;
        q.y = (matrix.value[4] + matrix.value[1]) / s;
        q.z = (matrix.value[8] + matrix.value[2]) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (matrix.value[8] - matrix.value[2]) / s;
        q.x = (matrix.value[4] + matrix.value[1]) / s;
        q.y = 0.25f * s;
        q.z = (matrix.value[9] + matrix.value[6]) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (matrix.value[1] - matrix.value[4]) / s;
        q.x = (matrix.value[8] + matrix.value[2]) / s;
        q.y = (matrix.value[9] + matrix.value[6]) / s;
        q.z = 0.25f * s;
    }
    const float magnitude = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (magnitude > kEpsilon) {
        q.x /= magnitude; q.y /= magnitude; q.z /= magnitude; q.w /= magnitude;
    }
    return q;
}

Animation::Mat4 localRotationMatrix(const TransformProperties& properties)
{
    Animation::Mat4 post = eulerMatrix(properties.post_rotation, properties.rotation_order);
    Animation::Mat4 inverse_post;
    if (!invertMatrix(post, &inverse_post)) inverse_post = identity();
    return Animation::multiply(
        Animation::multiply(eulerMatrix(properties.pre_rotation, properties.rotation_order),
                            eulerMatrix(properties.rotation_degrees, properties.rotation_order)),
        inverse_post
    );
}

Animation::Vec3 matrixTranslation(const Animation::Mat4& matrix)
{
    return {matrix.value[12], matrix.value[13], matrix.value[14]};
}

bool sourceGlobalMatrix(
    const Scene& scene,
    ObjectId model,
    const std::unordered_map<ObjectId, TransformProperties> *overrides,
    std::unordered_map<ObjectId, Animation::Mat4>& cache,
    std::unordered_set<ObjectId>& visiting,
    Animation::Mat4 *out,
    std::string *error)
{
    if (!out) return fail(error, "null FBX global transform destination");
    if (const auto found = cache.find(model); found != cache.end()) {
        *out = found->second;
        return true;
    }
    const Object *model_object = object(scene, model);
    if (!model_object || model_object->kind != "Model") {
        return fail(error, "FBX transform references missing Model id " + std::to_string(model));
    }
    if (!visiting.insert(model).second) {
        return fail(error, "FBX Model hierarchy contains a cycle at `" + model_object->name + "`");
    }

    TransformProperties properties;
    if (overrides) {
        if (const auto found = overrides->find(model); found != overrides->end()) properties = found->second;
        else if (!readTransformProperties(*model_object, &properties, error)) return false;
    } else if (!readTransformProperties(*model_object, &properties, error)) {
        return false;
    }

    Animation::Mat4 local;
    if (!localModelMatrix(properties, &local, error)) return false;
    const ObjectId parent_id = parentModel(scene, model);
    if (parent_id == 0) {
        visiting.erase(model);
        cache[model] = local;
        *out = local;
        return true;
    }

    Animation::Mat4 parent_global;
    if (!sourceGlobalMatrix(scene, parent_id, overrides, cache, visiting, &parent_global, error)) return false;
    const Object *parent_object = object(scene, parent_id);
    TransformProperties parent_properties;
    if (overrides) {
        if (const auto found = overrides->find(parent_id); found != overrides->end()) parent_properties = found->second;
        else if (!parent_object || !readTransformProperties(*parent_object, &parent_properties, error)) return false;
    } else if (!parent_object || !readTransformProperties(*parent_object, &parent_properties, error)) {
        return false;
    }

    const Animation::Mat4 parent_rotation = rotationOnly(parent_global);
    Animation::Mat4 inverse_parent_rotation;
    if (!invertMatrix(parent_rotation, &inverse_parent_rotation)) {
        return fail(error, "FBX parent rotation is singular for Model `" + model_object->name + "`");
    }
    Animation::Mat4 parent_translation = translation(matrixTranslation(parent_global));
    Animation::Mat4 inverse_parent_translation;
    if (!invertMatrix(parent_translation, &inverse_parent_translation)) {
        return fail(error, "FBX parent translation is singular for Model `" + model_object->name + "`");
    }
    const Animation::Mat4 parent_rs = Animation::multiply(inverse_parent_translation, parent_global);
    const Animation::Mat4 parent_gsm = Animation::multiply(inverse_parent_rotation, parent_rs);
    const Animation::Mat4 local_rotation = localRotationMatrix(properties);
    const Animation::Mat4 local_scale = scaling(properties.scale);

    Animation::Mat4 global_rs;
    if (properties.inherit_type == 0) {
        global_rs = Animation::multiply(
            Animation::multiply(Animation::multiply(parent_rotation, local_rotation), parent_gsm),
            local_scale);
    } else if (properties.inherit_type == 1) {
        global_rs = Animation::multiply(
            Animation::multiply(Animation::multiply(parent_rotation, parent_gsm), local_rotation),
            local_scale);
    } else if (properties.inherit_type == 2) {
        Animation::Mat4 inverse_parent_local_scale;
        if (!invertMatrix(scaling(parent_properties.scale), &inverse_parent_local_scale)) {
            return fail(error, "FBX parent local scale is singular for Model `" + model_object->name + "`");
        }
        const Animation::Mat4 parent_gsm_without_local = Animation::multiply(parent_gsm, inverse_parent_local_scale);
        global_rs = Animation::multiply(
            Animation::multiply(Animation::multiply(parent_rotation, local_rotation), parent_gsm_without_local),
            local_scale);
    } else {
        return fail(error, "unsupported transform inheritance mode " +
            std::to_string(properties.inherit_type) + " on Model `" + model_object->name + "`");
    }

    const Animation::Vec3 local_t = matrixTranslation(local);
    const Animation::Vec3 global_t = Animation::transformPoint(parent_global, local_t);
    const Animation::Mat4 global = Animation::multiply(translation(global_t), global_rs);

    visiting.erase(model);
    cache[model] = global;
    *out = global;
    return true;
}

} // namespace

bool invertMatrix(const Animation::Mat4& input, Animation::Mat4 *out)
{
    if (!out) return false;
    double augmented[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column_index = 0; column_index < 4; ++column_index) {
            augmented[row][column_index] = input.value[column_index * 4 + row];
        }
        augmented[row][4 + row] = 1.0;
    }
    for (int column_index = 0; column_index < 4; ++column_index) {
        int pivot = column_index;
        for (int row = column_index + 1; row < 4; ++row) {
            if (std::abs(augmented[row][column_index]) > std::abs(augmented[pivot][column_index])) pivot = row;
        }
        if (std::abs(augmented[pivot][column_index]) <= 1.0e-12) return false;
        if (pivot != column_index) {
            for (int item = 0; item < 8; ++item) std::swap(augmented[pivot][item], augmented[column_index][item]);
        }
        const double inv = 1.0 / augmented[column_index][column_index];
        for (int item = 0; item < 8; ++item) augmented[column_index][item] *= inv;
        for (int row = 0; row < 4; ++row) {
            if (row == column_index) continue;
            const double factor = augmented[row][column_index];
            for (int item = 0; item < 8; ++item) augmented[row][item] -= factor * augmented[column_index][item];
        }
    }
    Animation::Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column_index = 0; column_index < 4; ++column_index) {
            result.value[column_index * 4 + row] = static_cast<float>(augmented[row][4 + column_index]);
        }
    }
    *out = result;
    return true;
}

float linearDeterminant(const Animation::Mat4& matrix)
{
    const float a00 = matrix.value[0], a01 = matrix.value[4], a02 = matrix.value[8];
    const float a10 = matrix.value[1], a11 = matrix.value[5], a12 = matrix.value[9];
    const float a20 = matrix.value[2], a21 = matrix.value[6], a22 = matrix.value[10];
    return
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
}

Animation::Mat4 matrixFromFbxArray(const std::vector<double>& values)
{
    Animation::Mat4 result;
    if (values.size() < 16u) return result;
    for (std::size_t row = 0; row < 4u; ++row) {
        for (std::size_t column_index = 0; column_index < 4u; ++column_index) {
            result.value[column_index * 4u + row] = static_cast<float>(values[row * 4u + column_index]);
        }
    }
    return result;
}

bool readTransformProperties(const Object& object_value, TransformProperties *out, std::string *error)
{
    if (!out) return fail(error, "null FBX transform properties destination");
    TransformProperties result;
    result.translation = propertyVec3(object_value, "Lcl Translation", result.translation);
    result.rotation_degrees = propertyVec3(object_value, "Lcl Rotation", result.rotation_degrees);
    result.scale = propertyVec3(object_value, "Lcl Scaling", result.scale);
    result.pre_rotation = propertyVec3(object_value, "PreRotation", result.pre_rotation);
    result.post_rotation = propertyVec3(object_value, "PostRotation", result.post_rotation);
    result.rotation_offset = propertyVec3(object_value, "RotationOffset", result.rotation_offset);
    result.rotation_pivot = propertyVec3(object_value, "RotationPivot", result.rotation_pivot);
    result.scaling_offset = propertyVec3(object_value, "ScalingOffset", result.scaling_offset);
    result.scaling_pivot = propertyVec3(object_value, "ScalingPivot", result.scaling_pivot);
    result.geometric_translation = propertyVec3(object_value, "GeometricTranslation", result.geometric_translation);
    result.geometric_rotation = propertyVec3(object_value, "GeometricRotation", result.geometric_rotation);
    result.geometric_scale = propertyVec3(object_value, "GeometricScaling", result.geometric_scale);
    result.inherit_type = static_cast<int>(propertyInteger(object_value, "InheritType", 0));

    const std::int64_t order = propertyInteger(object_value, "RotationOrder", 0);
    if (order < 0 || order > 5) {
        return fail(error, "unsupported FBX rotation order " + std::to_string(order) +
            " on Model `" + object_value.name + "`");
    }
    result.rotation_order = static_cast<RotationOrder>(order);
    if (result.inherit_type < 0 || result.inherit_type > 2) {
        return fail(error, "unsupported transform inheritance mode " +
            std::to_string(result.inherit_type) + " on Model `" + object_value.name + "`");
    }

    *out = result;
    return true;
}

bool localModelMatrix(const TransformProperties& properties, Animation::Mat4 *out, std::string *error)
{
    if (!out) return fail(error, "null FBX local transform destination");
    Animation::Mat4 inverse_post;
    Animation::Mat4 inverse_rotation_pivot;
    Animation::Mat4 inverse_scaling_pivot;
    if (!invertMatrix(eulerMatrix(properties.post_rotation, properties.rotation_order), &inverse_post) ||
        !invertMatrix(translation(properties.rotation_pivot), &inverse_rotation_pivot) ||
        !invertMatrix(translation(properties.scaling_pivot), &inverse_scaling_pivot)) {
        return fail(error, "FBX transform contains a singular pivot/post-rotation matrix");
    }

    Animation::Mat4 result = translation(properties.translation);
    const std::array<Animation::Mat4, 11> factors{
        translation(properties.rotation_offset),
        translation(properties.rotation_pivot),
        eulerMatrix(properties.pre_rotation, properties.rotation_order),
        eulerMatrix(properties.rotation_degrees, properties.rotation_order),
        inverse_post,
        inverse_rotation_pivot,
        translation(properties.scaling_offset),
        translation(properties.scaling_pivot),
        scaling(properties.scale),
        inverse_scaling_pivot,
        identity(),
    };
    for (const auto& factor : factors) result = Animation::multiply(result, factor);
    *out = result;
    return true;
}

bool geometricMatrix(const TransformProperties& properties, Animation::Mat4 *out, std::string *error)
{
    if (!out) return fail(error, "null FBX geometric transform destination");
    *out = Animation::multiply(
        Animation::multiply(translation(properties.geometric_translation),
                            eulerMatrix(properties.geometric_rotation, properties.rotation_order)),
        scaling(properties.geometric_scale));
    return true;
}

bool globalModelMatrix(const Scene& scene, ObjectId model, Animation::Mat4 *out, std::string *error)
{
    std::unordered_map<ObjectId, Animation::Mat4> cache;
    std::unordered_set<ObjectId> visiting;
    Animation::Mat4 source;
    if (!sourceGlobalMatrix(scene, model, nullptr, cache, visiting, &source, error)) return false;
    *out = canonicalMatrix(scene.basis, source);
    return true;
}

bool globalModelMatrix(
    const Scene& scene,
    ObjectId model,
    const std::unordered_map<ObjectId, TransformProperties>& overrides,
    Animation::Mat4 *out,
    std::string *error)
{
    std::unordered_map<ObjectId, Animation::Mat4> cache;
    std::unordered_set<ObjectId> visiting;
    Animation::Mat4 source;
    if (!sourceGlobalMatrix(scene, model, &overrides, cache, visiting, &source, error)) return false;
    *out = canonicalMatrix(scene.basis, source);
    return true;
}

bool decomposeTrs(
    const Animation::Mat4& matrix,
    Animation::Transform *out,
    std::string *error,
    const std::string& context)
{
    if (!out) return fail(error, "null FBX TRS decomposition destination");
    Animation::Vec3 x = column(matrix, 0);
    Animation::Vec3 y = column(matrix, 1);
    Animation::Vec3 z = column(matrix, 2);
    float sx = length(x), sy = length(y), sz = length(z);
    if (sx <= kEpsilon || sy <= kEpsilon || sz <= kEpsilon) {
        return fail(error, "singular FBX transform cannot be represented as TRS in " + context);
    }
    Animation::Vec3 nx = mul(x, 1.0f / sx);
    Animation::Vec3 ny = mul(y, 1.0f / sy);
    Animation::Vec3 nz = mul(z, 1.0f / sz);
    if (std::abs(dot(nx, ny)) > 1.0e-4f ||
        std::abs(dot(nx, nz)) > 1.0e-4f ||
        std::abs(dot(ny, nz)) > 1.0e-4f) {
        return fail(error, "FBX transform contains shear not representable by Horse TRS in " + context);
    }
    if (dot(cross(nx, ny), nz) < 0.0f) {
        sx = -sx;
        nx = mul(nx, -1.0f);
    }
    Animation::Mat4 rotation;
    rotation.value[0] = nx.x; rotation.value[1] = nx.y; rotation.value[2] = nx.z;
    rotation.value[4] = ny.x; rotation.value[5] = ny.y; rotation.value[6] = ny.z;
    rotation.value[8] = nz.x; rotation.value[9] = nz.y; rotation.value[10] = nz.z;

    Animation::Transform result;
    result.translation = matrixTranslation(matrix);
    result.rotation = quatFromRotation(rotation);
    result.scale = {sx, sy, sz};
    *out = result;
    return true;
}

} // namespace Models::FbxInternal
