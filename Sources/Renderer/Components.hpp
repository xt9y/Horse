#ifndef RW_ENGINE_RENDERER_COMPONENTS_HPP
#define RW_ENGINE_RENDERER_COMPONENTS_HPP

#include "Ecs/Ecs.hpp"

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
};

struct Parent {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
};

struct MeshComponent {
    std::uint32_t mesh = UINT32_MAX;
    std::uint32_t material = UINT32_MAX;
};

struct RenderableComponent {
    bool visible = false;
};

enum class LightType {
    Directional,
    Point,
    Spot,
};

struct LightComponent {
    LightType type = LightType::Directional;
    Vec3 color{};
    float intensity = 0.0f;
    float range = 0.0f;
    float inner_cone_degrees = 20.0f;
    float outer_cone_degrees = 30.0f;
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
