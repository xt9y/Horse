#include "Renderer/Renderer.hpp"

#include "Renderer/Internal/DebugRenderPass.hpp"
#include "Renderer/Internal/FontPass.hpp"
#include "Renderer/GaussianSplat/GaussianSplat.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/Internal/ShadingState.hpp"
#include "Renderer/Internal/VolumetricsRender.hpp"
#include "Renderer/Volumetrics/Volumetrics.hpp"
#include "UI/Internal/RenderPass.hpp"

namespace Renderer {

void IRenderer::render(const Ecs::World& world)
{
    Internal::updateShadingState(world);
    Internal::FrameOutput output;
    output.global_illumination = GlobalIllumination::update(world);
    if (!renderScene(world, output)) return;
    if (!GaussianSplat::render(world, output)) return;
    if (!Volumetrics::render(world, output)) return;
    if (post_process_ && !post_process_->process(output)) return;
    if (!compose(output)) return;
    Debug::RenderPass::render(world, output);
    Internal::renderFonts(world, output);
    UI::Internal::RenderPass::render(output);
    present(output);
}

} // namespace Renderer
