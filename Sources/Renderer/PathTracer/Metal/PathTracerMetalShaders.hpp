#ifndef RW_ENGINE_RENDERER_PATHTRACER_METAL_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_METAL_SHADERS_HPP

#include "Renderer/PathTracer/Metal/PbrMetalShaders.hpp"
#include "Renderer/Trace/Metal/AdvancedMaterialShaders.hpp"
#include "Renderer/Trace/Metal/CameraProjectionShaders.hpp"

namespace Renderer::PathTracerMetalShaders {
inline const std::string source_storage = Renderer::Trace::Metal::advancedMaterialShaderSource(
    Renderer::Trace::Metal::cameraProjectionShaderSource(Renderer::PbrMetalShaders::source)
);
inline const char *source = source_storage.c_str();
}

#endif
