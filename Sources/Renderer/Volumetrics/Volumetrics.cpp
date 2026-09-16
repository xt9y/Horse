#include "Renderer/Volumetrics/Volumetrics.hpp"

#include "Renderer/Renderer.hpp"
#include "Renderer/ShadingState.hpp"
#include "Renderer/Volumetrics/VolumetricsSDLGPU.hpp"

#include <algorithm>

namespace Renderer::Volumetrics {
namespace {

Settings& storage()
{
    static Settings value;
    return value;
}

bool active(const Internal::ShadingState& shading)
{
    for (const Scenes::LightState& light : shading.lighting.lights) {
        if (light.valid && light.volumetric &&
            light.intensity > 0.0f && light.volumetric_intensity > 0.0f)
            return true;
    }
    return false;
}

} // namespace

Settings& settings()
{
    return storage();
}

const Settings& currentSettings()
{
    return storage();
}

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    Settings value = storage();
    if (!value.enabled || !active(Internal::shadingState())) return true;
    if (!output.command || !output.color_texture || !output.depth_texture ||
        output.depth == Internal::DepthSource::None)
        return true;

    value.resolution_divisor = std::max(value.resolution_divisor, 1);
    value.sample_count = std::max(value.sample_count, 1u);
    value.density = std::max(value.density, 0.0f);
    value.anisotropy = std::clamp(value.anisotropy, -0.95f, 0.95f);
    value.maximum_distance = std::max(value.maximum_distance, 0.001f);
    value.jitter = std::clamp(value.jitter, 0.0f, 1.0f);
    value.depth_falloff = std::max(value.depth_falloff, 0.0f);

    switch (output.api) {
    case Internal::GraphicsApi::SDLGPU:
        return Internal::renderVolumetricsSDLGPU(world, output, value);
    }
    return false;
}

void shutdown()
{
    Internal::shutdownVolumetricsSDLGPU();
}

} // namespace Renderer::Volumetrics
