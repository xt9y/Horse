#ifndef RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP

#include <string>

#define main_fragment horse_pbr_main_fragment
#include "Renderer/Rasterizer/OpenGL/PbrRasterizerShaders.hpp"
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

} // namespace

inline const std::string main_fragment_storage = compatibleMainFragment();
inline const char *main_fragment = main_fragment_storage.c_str();

} // namespace Renderer::RasterizerShaders

#endif
