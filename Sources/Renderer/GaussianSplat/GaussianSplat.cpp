#include "Renderer/GaussianSplat/GaussianSplat.hpp"

#include "Renderer/GaussianSplat/GaussianSplatSDLGPU.hpp"

namespace Renderer::GaussianSplat {

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (output.api == Internal::GraphicsApi::SDLGPU)
        return SDLGPU::render(world, output);
    return true;
}

void shutdown()
{
    SDLGPU::shutdown();
}

} // namespace Renderer::GaussianSplat
