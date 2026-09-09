#include "Font.hpp"

#include <utility>

namespace Font {

Ecs::Entity screen(
    Ecs::World& ecs,
    std::string text,
    Renderer::Vec2 position,
    float scale,
    Renderer::Vec4 color)
{
    const Ecs::Entity entity = ecs.createEntity();
    ecs.add<TextComponent>(entity, TextComponent{
        .text = std::move(text),
        .space = Space::Screen,
        .position = position,
        .scale = scale,
        .color = color,
    });
    return entity;
}

Ecs::Entity world(
    Ecs::World& ecs,
    std::string text,
    const Renderer::Transform& transform,
    float scale,
    Renderer::Vec4 color)
{
    const Ecs::Entity entity = ecs.createEntity();
    ecs.add<Renderer::Transform>(entity, transform);
    ecs.add<TextComponent>(entity, TextComponent{
        .text = std::move(text),
        .space = Space::World,
        .scale = scale,
        .color = color,
    });
    return entity;
}

} // namespace Font
