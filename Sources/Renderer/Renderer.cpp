#include "Renderer/Renderer.hpp"

#include "Renderer/Debug/Internal.hpp"
#include "Renderer/Fonts/FontPass.hpp"
#include "Renderer/GaussianSplat/GaussianSplat.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/ShadingState.hpp"
#include "UI/Internal.hpp"

#include <lwcgl/lwcgl.h>

namespace Renderer {

void IRenderer::render(const Ecs::World& world)
{
    Internal::updateShadingState(world);
    Internal::FrameOutput output;
    output.global_illumination = GlobalIllumination::update(world);
    if (!renderScene(world, output)) return;
    if (post_process_ && !post_process_->process(output)) return;
    if (!GaussianSplat::render(world, output)) return;
    if (!compose(output)) return;
    Debug::Internal::render(world, output);
    Internal::renderFonts(world, output);
    UI::Internal::render(output);
    present(output);
    if (output.api == Internal::GraphicsApi::OpenGL) {
        Display.updateNoMessages();
    }
}

} // namespace Renderer