#include "Renderer/PostProcess.hpp"

#include <algorithm>

namespace Renderer {

PostProcessState postProcessState(const Ecs::World& world)
{
    PostProcessState state;
    for (const Ecs::Entity entity : world.entities()) {
        const PostProcessComponent *component = world.get<PostProcessComponent>(entity);
        if (!component || !component->enabled) continue;
        state.exposure = std::max(component->exposure, 0.0f);
        state.bloom = component->bloom;
        state.bloom_threshold = std::max(component->bloom_threshold, 0.0f);
        state.bloom_intensity = std::max(component->bloom_intensity, 0.0f);
        state.motion_blur = component->motion_blur;
        state.motion_blur_strength = std::max(component->motion_blur_strength, 0.0f);
        state.motion_blur_samples = static_cast<std::uint8_t>(
            std::clamp<int>(component->motion_blur_samples, 1, 16)
        );
        state.anti_aliasing = component->anti_aliasing;
        break;
    }
    return state;
}

} // namespace Renderer
