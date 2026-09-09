#include "Renderer/PathTracer/PathTracerWorldFastShaders.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <cassert>
#include <cstring>

int main()
{
    const char *gl = Renderer::PathTracerShaders::trace;
    const char *gl_depth = std::strstr(gl, "Hit depth_hit = traceClosest");
    const char *gl_phase_return = std::strstr(gl, "if (pixel_phase != phase) return;");
    assert(gl_depth != nullptr);
    assert(gl_phase_return != nullptr);
    assert(gl_depth < gl_phase_return);
    assert(std::strstr(gl, "vec2 depth_uv = (vec2(pixel) + vec2(0.5)) / uResolution") != nullptr);

    const char *metal = Renderer::PathTracerMetalShaders::source;
    const char *metal_depth = std::strstr(metal, "Hit depth_hit = traceClosest");
    const char *metal_phase_return = std::strstr(metal, "if (pixel_phase != phase) return;");
    assert(metal_depth != nullptr);
    assert(metal_phase_return != nullptr);
    assert(metal_depth < metal_phase_return);
    assert(std::strstr(metal, "float2 depth_uv = (float2(pixel) + float2(0.5f)) / float2(size)") != nullptr);

    return 0;
}
