#ifndef RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP

#include <string>

#define main_fragment horse_pbr_main_fragment
#define sky_fragment horse_pbr_sky_fragment
#include "Renderer/Rasterizer/OpenGL/PbrRasterizerShaders.hpp"
#undef sky_fragment
#undef main_fragment

namespace Renderer::RasterizerShaders {
namespace {

inline void replaceOnce(std::string& source, const char *from, const char *to)
{
    const std::size_t position = source.find(from);
    if (position == std::string::npos) return;
    source.replace(position, std::char_traits<char>::length(from), to);
}

inline std::string compatibleMainFragment()
{
    std::string source = horse_pbr_main_fragment;
    replaceOnce(source, "uniform sampler2D uEnvironment;\n", "");
    replaceOnce(
        source,
        "    vec3 sampled = pow(max(texture2D(uEnvironment, environmentUv(direction)).rgb, vec3(0.0)), vec3(2.2));\n",
        "    vec3 sampled = average;\n"
    );
    return source;
}

inline std::string projectionAwareSkyFragment()
{
    std::string source = horse_pbr_sky_fragment;
    replaceOnce(
        source,
        "    vec3 direction = normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * uTanHalfFov) + uCameraUp * (ndc.y * uTanHalfFov));\n",
        "    float projection_scale = abs(uTanHalfFov);\n"
        "    bool orthographic = uTanHalfFov < 0.0;\n"
        "    vec3 direction = orthographic\n"
        "        ? normalize(uCameraForward)\n"
        "        : normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * projection_scale) +\n"
        "            uCameraUp * (ndc.y * projection_scale));\n"
    );
    replaceOnce(
        source,
        "    float previous_z = max(dot(direction, uPreviousCameraForward), 1.0e-5);\n"
        "    vec2 previous_ndc = vec2(\n"
        "        dot(direction, uPreviousCameraRight) / (previous_z * uAspect * uTanHalfFov),\n"
        "        dot(direction, uPreviousCameraUp) / (previous_z * uTanHalfFov)\n"
        "    );\n"
        "    vec2 velocity = (ndc - previous_ndc) * 0.5;",
        "    vec2 velocity = vec2(0.0);\n"
        "    if (!orthographic) {\n"
        "        float previous_z = max(dot(direction, uPreviousCameraForward), 1.0e-5);\n"
        "        vec2 previous_ndc = vec2(\n"
        "            dot(direction, uPreviousCameraRight) / (previous_z * uAspect * projection_scale),\n"
        "            dot(direction, uPreviousCameraUp) / (previous_z * projection_scale)\n"
        "        );\n"
        "        velocity = (ndc - previous_ndc) * 0.5;\n"
        "    }"
    );
    return source;
}

} // namespace

inline const std::string main_fragment_storage = compatibleMainFragment();
inline const char *main_fragment = main_fragment_storage.c_str();
inline const std::string sky_fragment_storage = projectionAwareSkyFragment();
inline const char *sky_fragment = sky_fragment_storage.c_str();

} // namespace Renderer::RasterizerShaders

#endif
