#include "Renderer/Renderer.hpp"

namespace Renderer {

void IRenderer::render(const Ecs::World& world)
{
    Internal::FrameOutput output;
    if (!renderScene(world, output)) return;
    present(output);
}

} // namespace Renderer
