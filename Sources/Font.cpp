#include "Font.hpp"

#include <algorithm>
#include <utility>

namespace Font {
namespace {

AtlasSettings settings;

} // namespace

void configureAtlas(
    std::string path,
    std::uint16_t columns,
    std::uint16_t rows,
    float screen_cell_pixels)
{
    settings.path = std::move(path);
    settings.columns = std::max<std::uint16_t>(columns, 1u);
    settings.rows = std::max<std::uint16_t>(rows, 1u);
    settings.screen_cell_pixels = std::max(screen_cell_pixels, 0.0f);
}

const AtlasSettings& atlas()
{
    return settings;
}

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
        .depth_test = false,
    });
    return entity;
}

Ecs::Entity world(
    Ecs::World& ecs,
    std::string text,
    const Renderer::Transform& transform,
    float scale,
    Renderer::Vec4 color,
    bool depth_test)
{
    const Ecs::Entity entity = ecs.createEntity();
    ecs.add<Renderer::Transform>(entity, transform);
    ecs.add<TextComponent>(entity, TextComponent{
        .text = std::move(text),
        .space = Space::World,
        .scale = scale,
        .color = color,
        .depth_test = depth_test,
    });
    return entity;
}

} // namespace Font
