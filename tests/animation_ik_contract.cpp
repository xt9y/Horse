#include "Animation/Animation.hpp"
#include "Animation/Ik.hpp"

#include <cassert>
#include <cmath>

namespace {

float distance(Animation::Vec3 a, Animation::Vec3 b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

Animation::Vec3 translation(const Animation::Mat4& matrix)
{
    return {matrix.value[12], matrix.value[13], matrix.value[14]};
}

} // namespace

int main()
{
    Animation::Skeleton skeleton;
    skeleton.name = "ik-contract";
    skeleton.bones.resize(3);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parent = -1;
    skeleton.bones[1].name = "middle";
    skeleton.bones[1].parent = 0;
    skeleton.bones[1].bind_local.translation = {1.0f, 0.0f, 0.0f};
    skeleton.bones[2].name = "end";
    skeleton.bones[2].parent = 1;
    skeleton.bones[2].bind_local.translation = {1.0f, 0.0f, 0.0f};

    Animation::Pose pose;
    pose.local = {
        skeleton.bones[0].bind_local,
        skeleton.bones[1].bind_local,
        skeleton.bones[2].bind_local,
    };
    Animation::rebuildPose(skeleton, &pose);
    const std::uint64_t before = pose.revision;

    Animation::IkTarget target;
    target.end_effector = 2u;
    target.chain_root = 0u;
    target.position = {1.0f, 1.35f, 0.0f};
    target.pole = {0.0f, 0.0f, 1.0f};
    target.use_pole = true;
    target.iterations = 24;
    target.tolerance = 0.0025f;

    assert(Animation::solveIk(skeleton, &pose, target));
    assert(pose.revision > before);
    assert(pose.global.size() == skeleton.bones.size());
    assert(pose.skin.size() == skeleton.bones.size());
    assert(distance(translation(pose.global[2]), target.position) < 0.03f);

    // Invalid chains are rejected without corrupting a valid pose.
    Animation::IkTarget invalid = target;
    invalid.end_effector = 99u;
    assert(!Animation::solveIk(skeleton, &pose, invalid));

    return 0;
}
