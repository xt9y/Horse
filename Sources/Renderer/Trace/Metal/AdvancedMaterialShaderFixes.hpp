#ifndef HORSE_RENDERER_TRACE_METAL_ADVANCED_MATERIAL_SHADER_FIXES_HPP
#define HORSE_RENDERER_TRACE_METAL_ADVANCED_MATERIAL_SHADER_FIXES_HPP

#include "Renderer/Trace/Metal/AdvancedMaterialShaders.hpp"

#include <string>

namespace Renderer::Trace::Metal {

inline void insertAdvancedMaterialArgument(std::string& source, const char *next_argument)
{
    const std::string marker = "materials,";
    const std::size_t next_length = std::char_traits<char>::length(next_argument);
    std::size_t position = 0u;
    while ((position = source.find(marker, position)) != std::string::npos) {
        std::size_t next = position + marker.size();
        while (next < source.size() &&
               (source[next] == ' ' || source[next] == '\t' || source[next] == '\r' || source[next] == '\n'))
        {
            ++next;
        }
        if (source.compare(next, next_length, next_argument) == 0) {
            constexpr const char *argument = " advanced_materials,";
            source.insert(position + marker.size(), argument);
            position += marker.size() + std::char_traits<char>::length(argument);
        } else {
            position += marker.size();
        }
    }
}

inline std::string finalizedAdvancedMaterialShaderSource(std::string source)
{
    source = advancedMaterialShaderSource(std::move(source));
    insertAdvancedMaterialArgument(source, "entity_visibility");
    insertAdvancedMaterialArgument(source, "uniforms");
    insertAdvancedMaterialArgument(source, "gi_data");

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
