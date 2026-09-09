#ifndef RW_ENGINE_FONT_HPP
#define RW_ENGINE_FONT_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

#include <cstdint>
#include <string>

namespace Font {

enum class Space : std::uint8_t {
    Screen,
    World,
};

struct TextComponent {
    std::string text;
    Space space = Space::Screen;
    Renderer::Vec2 position {0.0f, 0.0f};
    float scale = 1.0f;
    Renderer::Vec4 color {1.0f, 1.0f, 1.0f, 1.0f};
    bool depth_test = true;
};

Ecs::Entity screen(
    Ecs::World& ecs,
    std::string text,
    Renderer::Vec2 position = {},
    float scale = 1.0f,
    Renderer::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}
);

Ecs::Entity world(
    Ecs::World& ecs,
    std::string text,
    const Renderer::Transform& transform,
    float scale = 1.0f,
    Renderer::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
    bool depth_test = true
);

} // namespace Font

#endif
