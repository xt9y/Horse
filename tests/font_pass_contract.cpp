#include "Font.hpp"
#include "Renderer/FontPass.hpp"

#include <cassert>

int main()
{
    Ecs::World world;
    assert(!Renderer::Internal::fontDepthRequired(world));

    const Ecs::Entity screen = world.createEntity();
    world.add<Font::TextComponent>(screen, Font::TextComponent{
        .text = "screen",
        .space = Font::Space::Screen,
        .depth_test = true,
    });
    assert(!Renderer::Internal::fontDepthRequired(world));

    const Ecs::Entity visible = world.createEntity();
    world.add<Font::TextComponent>(visible, Font::TextComponent{
        .text = "world",
        .space = Font::Space::World,
        .depth_test = false,
    });
    assert(!Renderer::Internal::fontDepthRequired(world));

    const Ecs::Entity occluded = world.createEntity();
    world.add<Font::TextComponent>(occluded, Font::TextComponent{
        .text = "world depth",
        .space = Font::Space::World,
        .depth_test = true,
    });
    assert(Renderer::Internal::fontDepthRequired(world));
    return 0;
}
