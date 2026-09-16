#ifndef HORSE_RENDERER_INTERNAL_VOLUMETRICS_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_VOLUMETRICS_SDLGPU_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Volumetrics/Volumetrics.hpp"

namespace Renderer::Internal {
struct FrameOutput;

bool renderVolumetricsSDLGPU(
    const Ecs::World& world,
    FrameOutput& output,
    const Volumetrics::Settings& settings
);
void shutdownVolumetricsSDLGPU();

} // namespace Renderer::Internal

#endif
