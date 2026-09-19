#ifndef HORSE_RENDERER_REFLECTIONS_HPP
#define HORSE_RENDERER_REFLECTIONS_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Quality.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace Renderer::Reflections {

struct Settings {
    Quality quality = Quality::High;
    float strength = 1.0f;
};

struct ProbeComponent {
    bool enabled = true;
    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    Vec3 half_extents {5.0f, 5.0f, 5.0f};
    float blend_distance = 1.0f;
    float intensity = 1.0f;
    int priority = 0;
};

struct Probe {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    Vec3 position{};
    Vec3 half_extents{};
    float blend_distance = 0.0f;
    float intensity = 1.0f;
    int priority = 0;
};

struct State {
    std::vector<Probe> probes;
    std::uint64_t revision = 0u;
};

struct Blend {
    static constexpr std::uint32_t InvalidIndex = UINT32_MAX;

    std::array<std::uint32_t, 2> indices {InvalidIndex, InvalidIndex};
    std::array<float, 2> weights {0.0f, 0.0f};
    std::uint32_t count = 0u;
};

Settings& settings();
const Settings& currentSettings();

void setQuality(Quality value);
Quality quality();

State state(const Ecs::World& world);
float probeInfluence(const Probe& probe, Vec3 position);
Blend blend(const State& state, Vec3 position);

std::uint32_t limitEnvironmentMipLevels(
    std::uint32_t available_levels,
    Quality quality
);
std::uint32_t environmentMipLevels(
    std::uint32_t width,
    std::uint32_t height,
    Quality quality
);
float environmentLod(float roughness, std::uint32_t mip_levels);

} // namespace Renderer::Reflections

#endif
