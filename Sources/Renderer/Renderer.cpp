#include "Renderer/Renderer.hpp"

#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"

#include <lwcgl/lwcgl.h>

namespace Renderer {

void IRenderer::render(const Ecs::World& world)
{
    Internal::FrameOutput output;
    output.global_illumination = GlobalIllumination::update(world);
    if (!renderScene(world, output)) return;
    Internal::renderFonts(world, output);
    present(output);
    if (output.api == Internal::GraphicsApi::OpenGL) {
        Display.updateNoMessages();
    }
}

} // namespace Renderer
