#ifndef HORSE_RENDERER_COMPONENTS_HPP
#define HORSE_RENDERER_COMPONENTS_HPP

#include "Ecs/Ecs.hpp"

#include <array>
#include <cstdint>

namespace Renderer {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct Transform {
    Vec3 position {0.0f, 0.0f, 0.0f};
    Vec3 rotation {0.0f, 0.0f, 0.0f};
    Vec3 scale {1.0f, 1.0f, 1.0f};
    std::array<float, 16> matrix_override {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    bool matrix_override_enabled = false;
};

struct Parent {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
};

enum class RenderLayer {
    World,
    Overlay,
};

struct RenderLayerComponent {
    RenderLayer layer = RenderLayer::World;
};

struct InteractionComponent {
    bool selectable = true;
    bool hoverable = true;
};

enum class LightType {
    Directional,
    Point,
    Spot,
};

struct LightComponent {
    LightType type = LightType::Point;
    Vec3 color {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 200.0f;
    float inner_cone_degrees = 20.0f;
    float outer_cone_degrees = 30.0f;
};

struct ShadowComponent {
    bool enabled = true;
    float bias = 0.002f;
};

struct VolumetricLightComponent {
    bool enabled = true;
    float intensity = 1.0f;
};

struct GlobalIlluminationComponent {
    bool enabled = false;
    float intensity = 0.0f;
    std::uint8_t bounces = 0u;
    bool photon_mapping = false;
    std::uint32_t photon_count = 0u;
    float photon_radius = 0.0f;
};

} // namespace Renderer

#endif
