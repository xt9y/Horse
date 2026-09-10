#ifndef RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_METAL_HPP
#define RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_METAL_HPP

#ifdef __APPLE__

#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"

#include <lwmgl/lwmgl.h>

#include <cstdint>

namespace Renderer::Internal {

bool bindGlobalIlluminationMetal(
    LWMGLCommand command,
    const GlobalIllumination::Field *field,
    std::uint32_t buffer_index
);
void shutdownGlobalIlluminationMetal();

} // namespace Renderer::Internal

#endif

#endif
