#include "Renderer/Renderer.hpp"

#include "Renderer/FontPass.hpp"

namespace Renderer {

void IRenderer::render(const Ecs::World& world)
{
    Internal::FrameOutput output;
    if (!renderScene(world, output)) return;
    Internal::renderFonts(world, output);
    present(output);
}

} // namespace Renderer
