#ifndef HORSE_RENDERER_INTERNAL_SHADOW_CASCADES_HPP
#define HORSE_RENDERER_INTERNAL_SHADOW_CASCADES_HPP

#include <algorithm>

namespace Renderer::RasterizerSDLGPU::ShadowCascades {

inline constexpr float TransitionFraction = 0.1f;

constexpr float transitionStart(float previous_split, float split)
{
    const float span = std::max(split - previous_split, 0.0f);
    return split - span * TransitionFraction;
}

constexpr float transitionWeight(float depth, float start, float split)
{
    if (split <= start) return 0.0f;
    return std::clamp((depth - start) / (split - start), 0.0f, 1.0f);
}

} // namespace Renderer::RasterizerSDLGPU::ShadowCascades

#endif
