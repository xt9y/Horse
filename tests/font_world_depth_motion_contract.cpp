#include "Renderer/PathTracer/PathTracerWorldFastShaders.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <cassert>
#include <cstring>

int main()
{
    const char *gl = Renderer::PathTracerShaders::trace;
    const char *gl_clear = std::strstr(gl, "imageStore(uPrimaryDepth, pixel, vec4(INF, 0.0, 0.0, 0.0))");
    const char *gl_phase_return = std::strstr(gl, "if (pixel_phase != phase) return;");
    assert(gl_clear != nullptr);
    assert(gl_phase_return != nullptr);
    assert(gl_clear < gl_phase_return);
    assert(std::strstr(gl, "vec2 depth_uv = (vec2(pixel) + vec2(0.5)) / uResolution") != nullptr);
    assert(std::strstr(gl, "Hit depth_hit = traceClosest(uCameraPosition, depth_direction, INF)") != nullptr);
    assert(std::strstr(gl, "imageStore(uPrimaryDepth, pixel, vec4(depth_value, 0.0, 0.0, 1.0))") != nullptr);

    const char *metal = Renderer::PathTracerMetalShaders::source;
    const char *metal_clear = std::strstr(metal, "primary_depth.write(float4(INF, 0.0f, 0.0f, 0.0f), pixel)");
    const char *metal_phase_return = std::strstr(metal, "if (pixel_phase != phase) return;");
    assert(metal_clear != nullptr);
    assert(metal_phase_return != nullptr);
    assert(metal_clear < metal_phase_return);
    assert(std::strstr(metal, "float2 depth_uv = (float2(pixel) + float2(0.5f)) / float2(size)") != nullptr);
    assert(std::strstr(metal, "Hit depth_hit = traceClosest(uniforms.camera_position.xyz, depth_direction, INF") != nullptr);
    assert(std::strstr(metal, "primary_depth.write(float4(depth_value, 0.0f, 0.0f, 1.0f), pixel)") != nullptr);

    return 0;
}
