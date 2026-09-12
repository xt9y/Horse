#ifndef HORSE_RENDERER_TRACE_METAL_ADVANCED_MATERIAL_SHADER_FIXES_HPP
#define HORSE_RENDERER_TRACE_METAL_ADVANCED_MATERIAL_SHADER_FIXES_HPP

#include "Renderer/Trace/Metal/AdvancedMaterialShaders.hpp"

#include <string>

namespace Renderer::Trace::Metal {

inline std::string finalizedAdvancedMaterialShaderSource(std::string source)
{
    source = advancedMaterialShaderSource(std::move(source));
    const std::string from =
        "mix(iridescentF0(surface, dielectricF0(surface), max(dot(n, v), 0.0f)), surface.albedo, surface.metallic)";
    const std::string to =
        "mix(iridescentF0(surface, dielectricF0(surface), max(dot(surface.normal, view_direction), 0.0f)), surface.albedo, surface.metallic)";
    std::size_t position = 0u;
    while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from.size(), to);
        position += to.size();
    }
    return source;
}

} // namespace Renderer::Trace::Metal

#endif
