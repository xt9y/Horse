#ifndef RW_ENGINE_ANIMATION_IK_HPP
#define RW_ENGINE_ANIMATION_IK_HPP

#include "Animation/Animation.hpp"

#include <cstdint>

namespace Animation {

struct IkTarget {
    std::uint16_t end_effector = 0u;
    std::uint16_t chain_root = 0u;
    Vec3 position{};
    Vec3 pole{};
    bool use_pole = false;
    int iterations = 12;
    float tolerance = 0.001f;
    float weight = 1.0f;
};

// Re-evaluates hierarchy/global/skin matrices from pose.local and increments
// the pose revision. Useful after procedural animation edits as well as IK.
void rebuildPose(const Skeleton& skeleton, Pose *pose);

// Generic CCD IK. The end effector must descend from chain_root. The optional
// pole stabilizes the bend plane for two-bone and longer chains.
bool solveIk(const Skeleton& skeleton, Pose *pose, const IkTarget& target);

} // namespace Animation

#endif
