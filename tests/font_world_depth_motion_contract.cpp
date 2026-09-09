#include "Renderer/PathTracer/PathTracerWorldFastShaders.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <cassert>
#include <cstring>

int main()
{
    const char *gl = Renderer::PathTracerShaders::trace;
    const char *gl_depth = std::strstr(gl, "float deterministicDepth(vec2 sample_pixel)");
    const char *gl_phase_return = std::strstr(gl, "if (pixel_phase != phase) return;");
    assert(gl_depth != nullptr);
    assert(gl_phase_return != nullptr);
    assert(gl_depth < gl_phase_return);
    assert(std::strstr(gl, "const int MOVING_DEPTH_BLOCK = 2") != nullptr);
    assert(std::strstr(gl, "vec2 depth_uv = sample_pixel / uResolution") != nullptr);
    assert(std::strstr(gl, "imageStore(uPrimaryDepth, target, vec4(depth_value, 0.0, 0.0, 1.0))") != nullptr);
    assert(std::strstr(gl, "imageStore(uPrimaryDepth, pixel, vec4(primary_depth") == nullptr);

    const char *metal = Renderer::PathTracerMetalShaders::source;
    const char *metal_depth = std::strstr(metal, "float deterministicDepth(");
    const char *metal_phase_return = std::strstr(metal, "if (pixel_phase != phase) return;");
    assert(metal_depth != nullptr);
    assert(metal_phase_return != nullptr);
    assert(metal_depth < metal_phase_return);
    assert(std::strstr(metal, "constant uint MOVING_DEPTH_BLOCK = 2u") != nullptr);
    assert(std::strstr(metal, "float2 depth_uv = sample_pixel / float2(size)") != nullptr);
    assert(std::strstr(metal, "primary_depth.write(float4(depth_value, 0.0f, 0.0f, 1.0f), target)") != nullptr);
    assert(std::strstr(metal, "primary_depth.write(float4(primary_depth_value") == nullptr);

    return 0;
}
