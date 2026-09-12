#ifndef RW_ENGINE_RENDERER_PATHTRACER_OPENGL_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_OPENGL_SHADERS_HPP

#include <string>

#define trace horse_pbr_trace
#include "Renderer/PathTracer/OpenGL/PbrTraceShaders.hpp"
#undef trace
#include "Renderer/PathTracer/OpenGL/PathTracerPresentShaders.hpp"

namespace Renderer::PathTracerShaders {
namespace {

inline void replaceAll(std::string& source, const char *from, const char *to)
{
    const std::size_t from_size = std::char_traits<char>::length(from);
    const std::size_t to_size = std::char_traits<char>::length(to);
    std::size_t position = 0u;
    while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from_size, to);
        position += to_size;
    }
}

inline std::string projectionAwareTrace()
{
    std::string source = horse_pbr_trace;
    replaceAll(
        source,
        "    vec3 depth_direction = normalize(\n"
        "        uCameraForward + uCameraRight * (depth_ndc.x * uAspect * uTanHalfFov) +\n"
        "        uCameraUp * (depth_ndc.y * uTanHalfFov));\n"
        "    Hit depth_hit = traceClosest(uCameraPosition, depth_direction, INF, true);\n"
        "    if (!depth_hit.found) return INF;\n"
        "    return max(dot(depth_hit.position - uCameraPosition, normalize(uCameraForward)), RAY_EPSILON);",
        "    float projection_scale = abs(uTanHalfFov);\n"
        "    bool orthographic = uTanHalfFov < 0.0;\n"
        "    vec3 depth_origin = orthographic\n"
        "        ? uCameraPosition + uCameraRight * (depth_ndc.x * uAspect * projection_scale) +\n"
        "            uCameraUp * (depth_ndc.y * projection_scale)\n"
        "        : uCameraPosition;\n"
        "    vec3 depth_direction = orthographic\n"
        "        ? normalize(uCameraForward)\n"
        "        : normalize(uCameraForward + uCameraRight * (depth_ndc.x * uAspect * projection_scale) +\n"
        "            uCameraUp * (depth_ndc.y * projection_scale));\n"
        "    Hit depth_hit = traceClosest(depth_origin, depth_direction, INF, true);\n"
        "    if (!depth_hit.found) return INF;\n"
        "    return max(dot(depth_hit.position - depth_origin, normalize(uCameraForward)), RAY_EPSILON);"
    );
    replaceAll(
        source,
        "        vec3 direction = normalize(uCameraForward +\n"
        "            uCameraRight * (ndc.x * uAspect * uTanHalfFov) + uCameraUp * (ndc.y * uTanHalfFov));\n"
        "        sample_radiance += tracePath(uCameraPosition, direction);",
        "        float projection_scale = abs(uTanHalfFov);\n"
        "        bool orthographic = uTanHalfFov < 0.0;\n"
        "        vec3 ray_origin = orthographic\n"
        "            ? uCameraPosition + uCameraRight * (ndc.x * uAspect * projection_scale) +\n"
        "                uCameraUp * (ndc.y * projection_scale)\n"
        "            : uCameraPosition;\n"
        "        vec3 direction = orthographic\n"
        "            ? normalize(uCameraForward)\n"
        "            : normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * projection_scale) +\n"
        "                uCameraUp * (ndc.y * projection_scale));\n"
        "        sample_radiance += tracePath(ray_origin, direction);"
    );
    return source;
}

} // namespace

inline const std::string trace_storage = projectionAwareTrace();
inline const char *trace = trace_storage.c_str();

} // namespace Renderer::PathTracerShaders

#endif
