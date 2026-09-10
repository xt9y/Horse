#ifndef HORSE_RENDERER_POST_PROCESS_HPP
#define HORSE_RENDERER_POST_PROCESS_HPP

#include "Ecs/Ecs.hpp"

#include <cstdint>

namespace Renderer {

enum class AntiAliasing : std::uint8_t {
    None,
    Fxaa,
};

struct PostProcessComponent {
    bool enabled = true;
    float exposure = 1.0f;
    bool bloom = false;
    float bloom_threshold = 1.0f;
    float bloom_intensity = 0.12f;
    bool motion_blur = false;
    float motion_blur_strength = 0.5f;
    std::uint8_t motion_blur_samples = 8u;
    AntiAliasing anti_aliasing = AntiAliasing::Fxaa;
};

struct PostProcessState {
    float exposure = 1.0f;
    bool bloom = false;
    float bloom_threshold = 1.0f;
    float bloom_intensity = 0.12f;
    bool motion_blur = false;
    float motion_blur_strength = 0.5f;
    std::uint8_t motion_blur_samples = 8u;
    AntiAliasing anti_aliasing = AntiAliasing::Fxaa;
};

PostProcessState postProcessState(const Ecs::World& world);

} // namespace Renderer

#endif
