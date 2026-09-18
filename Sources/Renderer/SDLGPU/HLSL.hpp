#ifndef HORSE_RENDERER_SDLGPU_HLSL_HPP
#define HORSE_RENDERER_SDLGPU_HLSL_HPP

#include <cctype>
#include <string>
#include <string_view>

namespace Renderer::SDLGPU::HLSL {

inline bool identifierCharacter(char value)
{
    const unsigned char character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 || value == '_';
}

inline std::string replaceIdentifier(
    const char *source,
    std::string_view identifier,
    std::string_view replacement)
{
    std::string result = source ? source : "";
    std::size_t position = 0u;
    while ((position = result.find(identifier, position)) != std::string::npos) {
        const bool left = position > 0u && identifierCharacter(result[position - 1u]);
        const std::size_t end = position + identifier.size();
        const bool right = end < result.size() && identifierCharacter(result[end]);
        if (!left && !right) {
            result.replace(position, identifier.size(), replacement);
            position += replacement.size();
        } else {
            position = end;
        }
    }
    return result;
}

inline std::string shaderCrossCompatible(const char *source)
{
    // `triangle` is an HLSL primitive modifier. DXC rejects it as a local or
    // parameter identifier even though some frontends accepted it previously.
    return replaceIdentifier(source, "triangle", "tri");
}

} // namespace Renderer::SDLGPU::HLSL

#endif
