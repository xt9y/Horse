#ifndef RW_ENGINE_RENDERER_FONT_LAYOUT_HPP
#define RW_ENGINE_RENDERER_FONT_LAYOUT_HPP

#include "Renderer/Components.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Renderer::FontLayout {

struct GlyphQuad {
    std::uint8_t codepoint = 0u;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

void screen(std::string_view text, Vec2 position, float scale, std::vector<GlyphQuad>& out);
void world(std::string_view text, float scale, std::vector<GlyphQuad>& out);

} // namespace Renderer::FontLayout

#endif
