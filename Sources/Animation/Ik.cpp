#include "Animation/Ik.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Animation {
namespace {

constexpr float kEpsilon = 1.0e-8f;
constexpr float kPi = 3.14159265358979323846f;

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float lengthSquared(Vec3 value)
{
    return dot(value, value);
}

float length(Vec3 value)
{
    return std::sqrt(lengthSquared(value));
}

Vec3 normalized(Vec3 value)
{
    const float squared = lengthSquared(value);
    if (squared <= kEpsilon) return {};
    return multiply(value, 1.0f / std::sqrt(squared));
}

Quat normalized(Quat value)
{
    const float squared =
        value.x * value.x +
        value.y * value.y +
        value.z * value.z +
        value.w * value.w;
    if (squared <= kEpsilon) return {};
    const float inverse = 1.0f / std::sqrt(squared);
    return {
        value.x * inverse,
        value.y * inverse,
        value.z * inverse,
        value.w * inverse,
    };
}

Quat conjugate(Quat value)
{
    return {-value.x, -value.y, -value.z, value.w};
}

Quat quatMultiply(Quat a, Quat b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

Quat nlerpIdentity(Quat value, float weight)
{
    weight = std::clamp(weight, 0.0f, 1.0f);
    value = normalized(value);
    if (value.w < 0.0f) value = {-value.x, -value.y, -value.z, -value.w};
    return normalized({
        value.x * weight,
        value.y * weight,
        value.z * weight,
        1.0f + (value.w - 1.0f) * weight,
    });
}

Quat axisAngle(Vec3 axis, float radians)
{
    axis = normalized(axis);
    if (lengthSquared(axis) <= kEpsilon) return {};
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return normalized({axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)});
}

Quat fromTo(Vec3 from, Vec3 to)
{
    from = normalized(from);
    to = normalized(to);
    if (lengthSquared(from) <= kEpsilon || lengthSquared(to) <= kEpsilon) return {};

    const float cosine = std::clamp(dot(from, to), -1.0f, 1.0f);
    if (cosine > 1.0f - 1.0e-6f) return {};
    if (cosine < -1.0f + 1.0e-6f) {
        Vec3 axis = cross(from, {1.0f, 0.0f, 0.0f});
        if (lengthSquared(axis) <= kEpsilon) axis = cross(from, {0.0f, 1.0f, 0.0f});
        return axisAngle(axis, kPi);
    }

    const Vec3 axis = cross(from, to);
    return normalized({axis.x, axis.y, axis.z, 1.0f + cosine});
}

Quat rotationFromMatrix(const Mat4& value)
{
    Vec3 x{value.value[0], value.value[1], value.value[2]};
    Vec3 y{value.value[4], value.value[5], value.value[6]};
    Vec3 z{value.value[8], value.value[9], value.value[10]};
    x = normalized(x);
    y = normalized(y);
    z = normalized(z);

    const float m00 = x.x;
    const float m01 = y.x;
    const float m02 = z.x;
    const float m10 = x.y;
    const float m11 = y.y;
    const float m12 = z.y;
    const float m20 = x.z;
    const float m21 = y.z;
    const float m22 = z.z;
    const float trace = m00 + m11 + m22;

    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return normalized(q);
}

Vec3 matrixTranslation(const Mat4& value)
{
    return {value.value[12], value.value[13], value.value[14]};
}

bool buildGlobalRecursive(
    const Skeleton& skeleton,
    Pose *pose,
    std::size_t bone,
    std::vector<std::uint8_t> *state)
{
    if (!pose || !state || bone >= skeleton.bones.size() || bone >= pose->local.size()) return false;
    if ((*state)[bone] == 2u) return true;
    if ((*state)[bone] == 1u) return false;
    (*state)[bone] = 1u;

    const std::int32_t parent = skeleton.bones[bone].parent;
    const Mat4 local_matrix = matrix(pose->local[bone]);
    if (parent >= 0 && static_cast<std::size_t>(parent) < skeleton.bones.size()) {
        if (!buildGlobalRecursive(skeleton, pose, static_cast<std::size_t>(parent), state)) return false;
        pose->global[bone] = Animation::multiply(pose->global[static_cast<std::size_t>(parent)], local_matrix);
    } else {
        pose->global[bone] = local_matrix;
    }
    pose->skin[bone] = Animation::multiply(pose->global[bone], skeleton.bones[bone].inverse_bind);
    (*state)[bone] = 2u;
    return true;
}

bool rebuildPoseInternal(const Skeleton& skeleton, Pose *pose)
{
    if (!pose) return false;
    const std::size_t count = skeleton.bones.size();
    if (pose->local.size() < count) {
        const std::size_t old = pose->local.size();
        pose->local.resize(count);
        for (std::size_t bone = old; bone < count; ++bone) {
            pose->local[bone] = skeleton.bones[bone].bind_local;
        }
    } else if (pose->local.size() > count) {
        pose->local.resize(count);
    }
    pose->global.resize(count);
    pose->skin.resize(count);

    std::vector<std::uint8_t> state(count, 0u);
    for (std::size_t bone = 0u; bone < count; ++bone) {
        if (!buildGlobalRecursive(skeleton, pose, bone, &state)) return false;
    }
    return true;
}

Quat worldDeltaToLocal(const Skeleton& skeleton, const Pose& pose, std::size_t joint, Quat world_delta)
{
    const std::int32_t parent = skeleton.bones[joint].parent;
    if (parent < 0 || static_cast<std::size_t>(parent) >= pose.global.size()) return normalized(world_delta);
    const Quat parent_world = rotationFromMatrix(pose.global[static_cast<std::size_t>(parent)]);
    return normalized(quatMultiply(quatMultiply(conjugate(parent_world), world_delta), parent_world));
}

void applyWorldDelta(
    const Skeleton& skeleton,
    Pose *pose,
    std::size_t joint,
    Quat world_delta,
    float weight)
{
    if (!pose || joint >= pose->local.size()) return;
    const Quat weighted = nlerpIdentity(world_delta, weight);
    const Quat local_delta = worldDeltaToLocal(skeleton, *pose, joint, weighted);
    pose->local[joint].rotation = normalized(quatMultiply(local_delta, pose->local[joint].rotation));
}

bool chainToRoot(
    const Skeleton& skeleton,
    std::size_t end_effector,
    std::size_t chain_root,
    std::vector<std::size_t> *out)
{
    if (!out || end_effector >= skeleton.bones.size() || chain_root >= skeleton.bones.size()) return false;
    out->clear();
    std::size_t current = end_effector;
    for (std::size_t guard = 0u; guard <= skeleton.bones.size(); ++guard) {
        out->push_back(current);
        if (current == chain_root) return true;
        const std::int32_t parent = skeleton.bones[current].parent;
        if (parent < 0 || static_cast<std::size_t>(parent) >= skeleton.bones.size()) break;
        current = static_cast<std::size_t>(parent);
    }
    out->clear();
    return false;
}

void stabilizePole(
    const Skeleton& skeleton,
    Pose *pose,
    const std::vector<std::size_t>& chain,
    const IkTarget& target)
{
    if (!pose || !target.use_pole || chain.size() < 3u) return;
    const std::size_t root = chain.back();
    const std::size_t bend = chain[chain.size() - 2u];
    const std::size_t end = chain.front();

    const Vec3 root_position = matrixTranslation(pose->global[root]);
    const Vec3 bend_position = matrixTranslation(pose->global[bend]);
    const Vec3 end_position = matrixTranslation(pose->global[end]);
    const Vec3 axis = normalized(subtract(end_position, root_position));
    if (lengthSquared(axis) <= kEpsilon) return;

    Vec3 current = subtract(bend_position, root_position);
    current = subtract(current, multiply(axis, dot(current, axis)));
    Vec3 desired = subtract(target.pole, root_position);
    desired = subtract(desired, multiply(axis, dot(desired, axis)));
    current = normalized(current);
    desired = normalized(desired);
    if (lengthSquared(current) <= kEpsilon || lengthSquared(desired) <= kEpsilon) return;

    const float cosine = std::clamp(dot(current, desired), -1.0f, 1.0f);
    const float sine = dot(axis, cross(current, desired));
    const float angle = std::atan2(sine, cosine);
    applyWorldDelta(skeleton, pose, root, axisAngle(axis, angle), target.weight);
    rebuildPoseInternal(skeleton, pose);
}

} // namespace

void rebuildPose(const Skeleton& skeleton, Pose *pose)
{
    if (!pose || !rebuildPoseInternal(skeleton, pose)) return;
    ++pose->revision;
}

bool solveIk(const Skeleton& skeleton, Pose *pose, const IkTarget& target)
{
    if (!pose || skeleton.bones.empty()) return false;
    const std::size_t end = target.end_effector;
    const std::size_t root = target.chain_root;
    std::vector<std::size_t> chain;
    if (!chainToRoot(skeleton, end, root, &chain) || chain.size() < 2u) return false;
    if (!rebuildPoseInternal(skeleton, pose)) return false;

    const int iterations = std::clamp(target.iterations, 1, 128);
    const float tolerance = std::max(target.tolerance, 0.0f);
    const float weight = std::clamp(target.weight, 0.0f, 1.0f);
    if (weight <= 0.0f) return true;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        if (length(subtract(matrixTranslation(pose->global[end]), target.position)) <= tolerance) break;

        // chain[0] is the effector; rotating it cannot move itself, so start at its parent.
        for (std::size_t chain_index = 1u; chain_index < chain.size(); ++chain_index) {
            const std::size_t joint = chain[chain_index];
            const Vec3 joint_position = matrixTranslation(pose->global[joint]);
            const Vec3 end_position = matrixTranslation(pose->global[end]);
            const Vec3 current = subtract(end_position, joint_position);
            const Vec3 desired = subtract(target.position, joint_position);
            if (lengthSquared(current) <= kEpsilon || lengthSquared(desired) <= kEpsilon) continue;

            applyWorldDelta(skeleton, pose, joint, fromTo(current, desired), weight);
            if (!rebuildPoseInternal(skeleton, pose)) return false;
            if (length(subtract(matrixTranslation(pose->global[end]), target.position)) <= tolerance) break;
        }

        if (target.use_pole) stabilizePole(skeleton, pose, chain, target);
    }

    ++pose->revision;
    return true;
}

} // namespace Animation
