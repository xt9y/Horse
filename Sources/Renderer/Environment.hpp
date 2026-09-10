#ifndef HORSE_RENDERER_ENVIRONMENT_HPP
#define HORSE_RENDERER_ENVIRONMENT_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Components.hpp"

namespace Renderer {

enum class FogMode : std::uint8_t {
    None,
    Linear,
    Exponential,
};

struct EnvironmentComponent {
    bool enabled = true;
    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    Vec3 sky_color {0.02f, 0.03f, 0.05f};
    float intensity = 1.0f;
    float rotation_degrees = 0.0f;
    Vec3 ambient_color {1.0f, 1.0f, 1.0f};
    float ambient_intensity = 0.03f;
    FogMode fog = FogMode::None;
    Vec3 fog_color {0.5f, 0.6f, 0.7f};
    float fog_density = 0.0f;
    float fog_start = 0.0f;
    float fog_end = 1000.0f;
};

struct EnvironmentState {
    bool valid = false;
    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    Vec3 sky_color{};
    Vec3 average_color{};
    float intensity = 0.0f;
    float rotation_degrees = 0.0f;
    Vec3 ambient_color{};
    float ambient_intensity = 0.0f;
    FogMode fog = FogMode::None;
    Vec3 fog_color{};
    float fog_density = 0.0f;
    float fog_start = 0.0f;
    float fog_end = 0.0f;
};

EnvironmentState environmentState(const Ecs::World& world);
std::uint64_t environmentSignature(const EnvironmentState& environment);

} // namespace Renderer

#endif
