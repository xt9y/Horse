#include "Animation/Animation.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Animation {
namespace {

std::vector<Skeleton>& skeletons()
{
    static std::vector<Skeleton> values;
    return values;
}

std::vector<AnimationClip>& clips()
{
    static std::vector<AnimationClip> values;
    return values;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

Quat normalized(Quat value)
{
    const float length_squared =
        value.x * value.x +
        value.y * value.y +
        value.z * value.z +
        value.w * value.w;

    if (length_squared <= 1.0e-20f) return {};

    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

Quat slerp(Quat a, Quat b, float factor)
{
    a = normalized(a);
    b = normalized(b);

    float cosine = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cosine < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }

    factor = clamp01(factor);
    if (cosine > 0.9995f) {
        return normalized({
            a.x + (b.x - a.x) * factor,
            a.y + (b.y - a.y) * factor,
            a.z + (b.z - a.z) * factor,
            a.w + (b.w - a.w) * factor,
        });
    }

    const float angle = std::acos(std::clamp(cosine, -1.0f, 1.0f));
    const float sine = std::sin(angle);
    if (std::abs(sine) <= 1.0e-6f) return a;

    const float a_weight = std::sin((1.0f - factor) * angle) / sine;
    const float b_weight = std::sin(factor * angle) / sine;
    return {
        a.x * a_weight + b.x * b_weight,
        a.y * a_weight + b.y * b_weight,
        a.z * a_weight + b.z * b_weight,
        a.w * a_weight + b.w * b_weight,
    };
}

Transform sampleTrack(
    const AnimationClip& animation,
    std::size_t bone,
    float time,
    bool loop,
    const Transform& fallback)
{
    if (bone >= animation.tracks.size()) return fallback;
    const std::vector<Transform>& samples = animation.tracks[bone].samples;
    if (samples.empty()) return fallback;
    if (samples.size() == 1u || animation.duration <= 0.0f || animation.sample_rate <= 0.0f) {
        return samples.front();
    }

    float sample_time = time;
    if (loop) {
        sample_time = std::fmod(std::max(sample_time, 0.0f), animation.duration);
    } else {
        sample_time = std::clamp(sample_time, 0.0f, animation.duration);
    }

    const float frame = sample_time * animation.sample_rate;
    const std::size_t first = std::min<std::size_t>(
        static_cast<std::size_t>(frame),
        samples.size() - 1u
    );
    const std::size_t second = std::min(first + 1u, samples.size() - 1u);
    return blend(samples[first], samples[second], frame - static_cast<float>(first));
}

void evaluate(AnimatorComponent& animator, const Skeleton& skeleton_asset)
{
    const std::size_t bone_count = skeleton_asset.bones.size();
    animator.pose.local.resize(bone_count);
    animator.pose.global.resize(bone_count);
    animator.pose.skin.resize(bone_count);

    const AnimationClip *current = clip(animator.clip);
    const AnimationClip *next = clip(animator.next_clip);
    const float blend_factor = next && animator.blend_duration > 0.0f
        ? clamp01(animator.blend_time / animator.blend_duration)
        : 0.0f;

    for (std::size_t bone = 0u; bone < bone_count; ++bone) {
        const Transform& bind = skeleton_asset.bones[bone].bind_local;
        Transform local = current
            ? sampleTrack(*current, bone, animator.time, animator.loop, bind)
            : bind;

        if (next) {
            const Transform target = sampleTrack(
                *next,
                bone,
                animator.next_time,
                animator.next_loop,
                bind
            );
            local = blend(local, target, blend_factor);
        }

        animator.pose.local[bone] = local;
        const Mat4 local_matrix = matrix(local);
        const std::int32_t parent = skeleton_asset.bones[bone].parent;
        animator.pose.global[bone] = parent >= 0 && static_cast<std::size_t>(parent) < bone
            ? multiply(animator.pose.global[static_cast<std::size_t>(parent)], local_matrix)
            : local_matrix;
        animator.pose.skin[bone] = multiply(
            animator.pose.global[bone],
            skeleton_asset.bones[bone].inverse_bind
        );
    }

    ++animator.pose.revision;
}

} // namespace

SkeletonHandle registerSkeleton(Skeleton value)
{
    const SkeletonHandle handle = static_cast<SkeletonHandle>(skeletons().size());
    skeletons().push_back(std::move(value));
    return handle;
}

ClipHandle registerClip(AnimationClip value)
{
    const ClipHandle handle = static_cast<ClipHandle>(clips().size());
    clips().push_back(std::move(value));
    return handle;
}

const Skeleton *skeleton(SkeletonHandle handle)
{
    return handle < skeletons().size() ? &skeletons()[handle] : nullptr;
}

const AnimationClip *clip(ClipHandle handle)
{
    return handle < clips().size() ? &clips()[handle] : nullptr;
}

void clearAssets()
{
    skeletons().clear();
    clips().clear();
}

void StateMachine::add(State state)
{
    const auto found = lookup_.find(state.name);
    if (found != lookup_.end()) {
        states_[found->second] = std::move(state);
        return;
    }

    lookup_.emplace(state.name, states_.size());
    states_.push_back(std::move(state));
}

const State *StateMachine::find(std::string_view name) const
{
    const auto found = lookup_.find(std::string(name));
    return found == lookup_.end() ? nullptr : &states_[found->second];
}

void play(
    AnimatorComponent& animator,
    ClipHandle clip_handle,
    float blend_seconds,
    bool loop,
    float speed)
{
    if (!clip(clip_handle)) return;

    if (animator.clip == INVALID_CLIP || blend_seconds <= 0.0f) {
        animator.clip = clip_handle;
        animator.next_clip = INVALID_CLIP;
        animator.time = 0.0f;
        animator.next_time = 0.0f;
        animator.speed = speed;
        animator.next_speed = 1.0f;
        animator.blend_time = 0.0f;
        animator.blend_duration = 0.0f;
        animator.loop = loop;
        animator.next_loop = true;
    } else if (animator.clip != clip_handle) {
        animator.next_clip = clip_handle;
        animator.next_time = 0.0f;
        animator.next_speed = speed;
        animator.next_loop = loop;
        animator.blend_time = 0.0f;
        animator.blend_duration = std::max(blend_seconds, 0.0f);
    }

    animator.playing = true;
}

bool playState(
    AnimatorComponent& animator,
    const StateMachine& machine,
    std::string_view state,
    float blend_seconds)
{
    const State *entry = machine.find(state);
    if (!entry) return false;

    play(animator, entry->clip, blend_seconds, entry->loop, entry->speed);
    return true;
}

void stop(AnimatorComponent& animator)
{
    animator.playing = false;
}

void System::update(Ecs::World& world, float delta_seconds) const
{
    bool changed = false;

    world.each<AnimatorComponent>(
        [&](Ecs::Entity, AnimatorComponent& animator) {
            const Skeleton *skeleton_asset = skeleton(animator.skeleton);
            if (!skeleton_asset) return;

            const bool needs_initial_pose = animator.pose.skin.size() != skeleton_asset->bones.size();
            if (!animator.playing && !needs_initial_pose) return;

            const float safe_delta = std::max(delta_seconds, 0.0f);
            if (animator.playing) {
                animator.time += safe_delta * animator.speed;

                if (animator.next_clip != INVALID_CLIP) {
                    animator.next_time += safe_delta * animator.next_speed;
                    animator.blend_time += safe_delta;

                    if (
                        animator.blend_duration <= 0.0f ||
                        animator.blend_time >= animator.blend_duration)
                    {
                        animator.clip = animator.next_clip;
                        animator.time = animator.next_time;
                        animator.speed = animator.next_speed;
                        animator.loop = animator.next_loop;
                        animator.next_clip = INVALID_CLIP;
                        animator.next_time = 0.0f;
                        animator.next_speed = 1.0f;
                        animator.next_loop = true;
                        animator.blend_time = 0.0f;
                        animator.blend_duration = 0.0f;
                    }
                }
            }

            bool stop_after_evaluate = false;
            if (animator.playing && animator.next_clip == INVALID_CLIP && !animator.loop) {
                const AnimationClip *current = clip(animator.clip);
                if (current && current->duration > 0.0f && animator.time >= current->duration) {
                    animator.time = current->duration;
                    stop_after_evaluate = true;
                }
            }

            evaluate(animator, *skeleton_asset);
            if (stop_after_evaluate) animator.playing = false;
            changed = true;
        }
    );

    if (changed) world.markChanged(Ecs::ChangeKind::Animation);
}

Transform blend(const Transform& a, const Transform& b, float factor)
{
    factor = clamp01(factor);
    return {
        {
            a.translation.x + (b.translation.x - a.translation.x) * factor,
            a.translation.y + (b.translation.y - a.translation.y) * factor,
            a.translation.z + (b.translation.z - a.translation.z) * factor,
        },
        slerp(a.rotation, b.rotation, factor),
        {
            a.scale.x + (b.scale.x - a.scale.x) * factor,
            a.scale.y + (b.scale.y - a.scale.y) * factor,
            a.scale.z + (b.scale.z - a.scale.z) * factor,
        },
    };
}

Mat4 matrix(const Transform& transform)
{
    const Quat q = normalized(transform.rotation);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;

    Mat4 result;
    result.value = {
        (1.0f - 2.0f * (yy + zz)) * transform.scale.x,
        (2.0f * (xy + wz)) * transform.scale.x,
        (2.0f * (xz - wy)) * transform.scale.x,
        0.0f,

        (2.0f * (xy - wz)) * transform.scale.y,
        (1.0f - 2.0f * (xx + zz)) * transform.scale.y,
        (2.0f * (yz + wx)) * transform.scale.y,
        0.0f,

        (2.0f * (xz + wy)) * transform.scale.z,
        (2.0f * (yz - wx)) * transform.scale.z,
        (1.0f - 2.0f * (xx + yy)) * transform.scale.z,
        0.0f,

        transform.translation.x,
        transform.translation.y,
        transform.translation.z,
        1.0f,
    };
    return result;
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += a.value[k * 4 + row] * b.value[column * 4 + k];
            }
            result.value[column * 4 + row] = value;
        }
    }
    return result;
}

Vec3 transformPoint(const Mat4& value, Vec3 point)
{
    return {
        value.value[0] * point.x + value.value[4] * point.y + value.value[8] * point.z + value.value[12],
        value.value[1] * point.x + value.value[5] * point.y + value.value[9] * point.z + value.value[13],
        value.value[2] * point.x + value.value[6] * point.y + value.value[10] * point.z + value.value[14],
    };
}

Vec3 transformVector(const Mat4& value, Vec3 vector)
{
    return {
        value.value[0] * vector.x + value.value[4] * vector.y + value.value[8] * vector.z,
        value.value[1] * vector.x + value.value[5] * vector.y + value.value[9] * vector.z,
        value.value[2] * vector.x + value.value[6] * vector.y + value.value[10] * vector.z,
    };
}

bool skinVertex(
    const Pose& pose,
    const SkinWeights& skin,
    Vec3 position,
    Vec3 normal,
    Vec3 *out_position,
    Vec3 *out_normal)
{
    if (!out_position || !out_normal) return false;

    Vec3 skinned_position {};
    Vec3 skinned_normal {};
    float total_weight = 0.0f;

    for (std::size_t influence = 0u; influence < skin.weights.size(); ++influence) {
        const float weight = skin.weights[influence];
        const std::size_t joint = skin.joints[influence];
        if (weight <= 0.0f || joint >= pose.skin.size()) continue;

        const Vec3 transformed_position = transformPoint(pose.skin[joint], position);
        const Vec3 transformed_normal = transformVector(pose.skin[joint], normal);

        skinned_position.x += transformed_position.x * weight;
        skinned_position.y += transformed_position.y * weight;
        skinned_position.z += transformed_position.z * weight;
        skinned_normal.x += transformed_normal.x * weight;
        skinned_normal.y += transformed_normal.y * weight;
        skinned_normal.z += transformed_normal.z * weight;
        total_weight += weight;
    }

    if (total_weight <= 1.0e-8f) {
        *out_position = position;
        *out_normal = normal;
        return false;
    }

    const float inverse_weight = 1.0f / total_weight;
    skinned_position.x *= inverse_weight;
    skinned_position.y *= inverse_weight;
    skinned_position.z *= inverse_weight;

    const float normal_length_squared =
        skinned_normal.x * skinned_normal.x +
        skinned_normal.y * skinned_normal.y +
        skinned_normal.z * skinned_normal.z;

    if (normal_length_squared > 1.0e-20f) {
        const float inverse_length = 1.0f / std::sqrt(normal_length_squared);
        skinned_normal.x *= inverse_length;
        skinned_normal.y *= inverse_length;
        skinned_normal.z *= inverse_length;
    } else {
        skinned_normal = normal;
    }

    *out_position = skinned_position;
    *out_normal = skinned_normal;
    return true;
}

} // namespace Animation
