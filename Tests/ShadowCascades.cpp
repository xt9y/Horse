#include "Renderer/Internal/ShadowCascades.hpp"

#include <cassert>
#include <cmath>

int main()
{
    using namespace Renderer::RasterizerSDLGPU::ShadowCascades;

    constexpr float near_split = 0.1f;
    constexpr float far_split = 10.0f;
    constexpr float start = transitionStart(near_split, far_split);

    static_assert(start > near_split);
    static_assert(start < far_split);
    static_assert(transitionWeight(start - 0.1f, start, far_split) == 0.0f);
    static_assert(transitionWeight(start, start, far_split) == 0.0f);
    static_assert(transitionWeight(far_split, start, far_split) == 1.0f);
    static_assert(transitionWeight(far_split + 1.0f, start, far_split) == 1.0f);
    static_assert(transitionWeight(1.0f, 2.0f, 2.0f) == 0.0f);

    const float middle = (start + far_split) * 0.5f;
    assert(std::abs(transitionWeight(middle, start, far_split) - 0.5f) < 1.0e-5f);
    return 0;
}
