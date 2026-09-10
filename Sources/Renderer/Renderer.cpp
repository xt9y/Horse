#include "Renderer/Renderer.hpp"

#include "Renderer/Fonts/FontPass.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "UI/Internal.hpp"

#include <lwcgl/lwcgl.h>

namespace Renderer {

void IRenderer::render(const Ecs::World& world)
{
    Internal::FrameOutput output;
    output.global_illumination = GlobalIllumination::update(world);
    if (!renderScene(world, output)) return;
    Internal::renderFonts(world, output);
    UI::Internal::render(output);
    present(output);
    if (output.api == Internal::GraphicsApi::OpenGL) {
        Display.updateNoMessages();
    }
}

} // namespace Renderer
