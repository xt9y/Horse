#ifndef HORSE_RENDERER_GLOBAL_ILLUMINATION_SDLGPU_HPP
#define HORSE_RENDERER_GLOBAL_ILLUMINATION_SDLGPU_HPP

#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>

namespace Renderer::Internal {

bool bindGlobalIlluminationSDLGPU(
    SDL_GPURenderPass *pass,
    const GlobalIllumination::Field *field,
    std::uint32_t slot = 4u
);
bool bindGlobalIlluminationSDLGPU(
    SDL_GPUComputePass *pass,
    const GlobalIllumination::Field *field,
    std::uint32_t slot = 4u
);
void shutdownGlobalIlluminationSDLGPU();

} // namespace Renderer::Internal

#endif
