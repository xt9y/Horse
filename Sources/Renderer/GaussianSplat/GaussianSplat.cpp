#include "Renderer/GaussianSplat/GaussianSplat.hpp"

#include "Renderer/GaussianSplat/GaussianSplatSDLGPU.hpp"

namespace Renderer::GaussianSplat {
namespace {

Settings current_settings;

} // namespace

Settings& settings()
{
    return current_settings;
}

const Settings& currentSettings()
{
    return current_settings;
}

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!current_settings.enabled) return true;
    if (output.api == Internal::GraphicsApi::SDLGPU)
        return SDLGPU::render(world, output);
    return true;
}

void shutdown()
{
    SDLGPU::shutdown();
}

} // namespace Renderer::GaussianSplat
