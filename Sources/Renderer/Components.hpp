#ifndef RW_ENGINE_RENDERER_COMPONENTS_HPP
#define RW_ENGINE_RENDERER_COMPONENTS_HPP

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

struct MeshComponent {
    std::uint32_t mesh = UINT32_MAX;
    std::uint32_t material = UINT32_MAX;
};

struct RenderableComponent {
    bool visible = true;
};

enum class LightType {
    Directional,
    Point,
    Spot,
};

struct LightComponent {
    LightType type = LightType::Directional;
    Vec3 color {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

} // namespace Renderer

#endif
