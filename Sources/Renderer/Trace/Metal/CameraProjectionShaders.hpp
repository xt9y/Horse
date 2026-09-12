#ifndef HORSE_RENDERER_TRACE_METAL_CAMERA_PROJECTION_SHADERS_HPP
#define HORSE_RENDERER_TRACE_METAL_CAMERA_PROJECTION_SHADERS_HPP

#include <string>

namespace Renderer::Trace::Metal {
namespace {

inline void replaceProjectionSource(std::string& source, const char *from, const char *to)
{
    const std::size_t from_size = std::char_traits<char>::length(from);
    const std::size_t to_size = std::char_traits<char>::length(to);
    std::size_t position = 0u;
    while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from_size, to);
        position += to_size;
    }
}

} // namespace

inline std::string cameraProjectionShaderSource(const char *base_source)
{
    std::string source = base_source;

    replaceProjectionSource(
        source,
        "    float3 direction = normalize(uniforms.camera_forward.xyz +\n"
        "        uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +\n"
        "        uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w));\n"
        "    Hit hit = traceClosestAlpha(uniforms.camera_position.xyz, direction, INF, true, acceleration_structure,\n"
        "        triangles, materials, entity_visibility, uniforms, textures, material_sampler);\n"
        "    if (!hit.found) return INF;\n"
        "    return max(dot(hit.position - uniforms.camera_position.xyz, normalize(uniforms.camera_forward.xyz)), RAY_EPSILON);",
        "    float projection_scale = abs(uniforms.light_color_tan_half_fov.w);\n"
        "    bool orthographic = uniforms.light_color_tan_half_fov.w < 0.0f;\n"
        "    float3 ray_origin = orthographic\n"
        "        ? uniforms.camera_position.xyz +\n"
        "            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * projection_scale) +\n"
        "            uniforms.camera_up.xyz * (ndc.y * projection_scale)\n"
        "        : uniforms.camera_position.xyz;\n"
        "    float3 direction = orthographic\n"
        "        ? normalize(uniforms.camera_forward.xyz)\n"
        "        : normalize(uniforms.camera_forward.xyz +\n"
        "            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * projection_scale) +\n"
        "            uniforms.camera_up.xyz * (ndc.y * projection_scale));\n"
        "    Hit hit = traceClosestAlpha(ray_origin, direction, INF, true, acceleration_structure,\n"
        "        triangles, materials, entity_visibility, uniforms, textures, material_sampler);\n"
        "    if (!hit.found) return INF;\n"
        "    return max(dot(hit.position - ray_origin, normalize(uniforms.camera_forward.xyz)), RAY_EPSILON);"
    );

    replaceProjectionSource(
        source,
        "    float3 direction = normalize(uniforms.camera_forward.xyz +\n"
        "        uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +\n"
        "        uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w));\n"
        "    float depth = INF;\n"
        "    float3 radiance = shade(uniforms.camera_position.xyz, direction, acceleration_structure, triangles,\n"
        "        materials, gi_data, entity_visibility, uniforms, textures, material_sampler, depth);",
        "    float projection_scale = abs(uniforms.light_color_tan_half_fov.w);\n"
        "    bool orthographic = uniforms.light_color_tan_half_fov.w < 0.0f;\n"
        "    float3 ray_origin = orthographic\n"
        "        ? uniforms.camera_position.xyz +\n"
        "            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * projection_scale) +\n"
        "            uniforms.camera_up.xyz * (ndc.y * projection_scale)\n"
        "        : uniforms.camera_position.xyz;\n"
        "    float3 direction = orthographic\n"
        "        ? normalize(uniforms.camera_forward.xyz)\n"
        "        : normalize(uniforms.camera_forward.xyz +\n"
        "            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * projection_scale) +\n"
        "            uniforms.camera_up.xyz * (ndc.y * projection_scale));\n"
        "    float depth = INF;\n"
        "    float3 radiance = shade(ray_origin, direction, acceleration_structure, triangles,\n"
        "        materials, gi_data, entity_visibility, uniforms, textures, material_sampler, depth);"
    );

    replaceProjectionSource(
        source,
        "        float3 direction = normalize(uniforms.camera_forward.xyz +\n"
        "            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +\n"
        "            uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w));\n"
        "        float depth = INF;\n"
        "        sample_radiance += shade(uniforms.camera_position.xyz, direction, acceleration_structure, triangles,\n"
        "            materials, gi_data, entity_visibility, uniforms, textures, material_sampler, depth);",
        "        float projection_scale = abs(uniforms.light_color_tan_half_fov.w);\n"
        "        bool orthographic = uniforms.light_color_tan_half_fov.w < 0.0f;\n"
        "        float3 ray_origin = orthographic\n"
        "            ? uniforms.camera_position.xyz +\n"
        "                uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * projection_scale) +\n"
        "                uniforms.camera_up.xyz * (ndc.y * projection_scale)\n"
        "            : uniforms.camera_position.xyz;\n"
        "        float3 direction = orthographic\n"
        "            ? normalize(uniforms.camera_forward.xyz)\n"
        "            : normalize(uniforms.camera_forward.xyz +\n"
        "                uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * projection_scale) +\n"
        "                uniforms.camera_up.xyz * (ndc.y * projection_scale));\n"
        "        float depth = INF;\n"
        "        sample_radiance += shade(ray_origin, direction, acceleration_structure, triangles,\n"
        "            materials, gi_data, entity_visibility, uniforms, textures, material_sampler, depth);"
    );

    return source;
}

} // namespace Renderer::Trace::Metal

#endif
