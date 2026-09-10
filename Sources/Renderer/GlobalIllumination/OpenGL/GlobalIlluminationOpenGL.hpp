#ifndef RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_OPENGL_HPP
#define RW_ENGINE_RENDERER_GLOBAL_ILLUMINATION_OPENGL_HPP

#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"

namespace Renderer::Internal {

void bindGlobalIlluminationOpenGL(const GlobalIllumination::Field *field);
void shutdownGlobalIlluminationOpenGL();

} // namespace Renderer::Internal

#endif
