#include "Renderer/Renderer.hpp"

#include "Renderer/Features.hpp"
#include "Renderer/Internal/DebugRenderPass.hpp"
#include "Renderer/Internal/Display.hpp"
#include "Renderer/Internal/FontPass.hpp"
#include "Renderer/Internal/LinesRenderPass.hpp"
#include "Renderer/GaussianSplat/GaussianSplat.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/Internal/ShadingState.hpp"
#include "Renderer/Internal/VolumetricsRender.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/Volumetrics/Volumetrics.hpp"
#include "UI/Internal/RenderPass.hpp"

#include <SDL3/SDL_gpu.h>

namespace Renderer {

bool waitIdle()
{
    SDL_GPUDevice *device = SDLGPU::device();
    return device && SDL_WaitForGPUIdle(device);
}

void IRenderer::render(const Ecs::World& world)
{
    Internal::updateShadingState(world);
    const Features::Settings& features = Features::currentSettings();

    Internal::FrameOutput output;
    if (features.global_illumination && GlobalIllumination::enabled(world))
        output.global_illumination = GlobalIllumination::update(world);
    if (!renderScene(world, output)) return;
    if (features.gaussian_splat && !GaussianSplat::render(world, output)) return;
    if (features.volumetrics && !Volumetrics::render(world, output)) return;
    if (post_process_ && !post_process_->process(output)) return;
    if (!Internal::renderDisplay(world, output)) return;
    if (!compose(output)) return;
    Lines::Internal::render(world, output);
    Debug::RenderPass::render(world, output);
    Internal::renderFonts(world, output);
    UI::Internal::RenderPass::render(output);
    present(output);
}

} // namespace Renderer
