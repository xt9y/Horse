#include "Renderer/FontLayout.hpp"

#include <algorithm>

namespace Renderer::FontLayout {
namespace {

void layout(
    std::string_view text,
    float origin_x,
    float origin_y,
    float cell,
    float vertical_direction,
    std::vector<GlyphQuad>& out)
{
    out.clear();
    const float safe_cell = std::max(cell, 0.0f);
    float x = origin_x;
    float y = origin_y;

    for (const unsigned char codepoint : text) {
        if (codepoint == '\r') continue;
        if (codepoint == '\n') {
            x = origin_x;
            y += safe_cell * vertical_direction;
            continue;
        }
        if (codepoint == '\t') {
            x += safe_cell * 4.0f;
            continue;
        }

        out.push_back(GlyphQuad{
            .codepoint = codepoint,
            .x0 = x,
            .y0 = y,
            .x1 = x + safe_cell,
            .y1 = y + safe_cell * vertical_direction,
        });
        x += safe_cell;
    }
}

} // namespace

void screen(std::string_view text, Vec2 position, float scale, std::vector<GlyphQuad>& out)
{
    layout(text, position.x, position.y, std::max(scale, 0.0f) * 8.0f, 1.0f, out);
}

void world(std::string_view text, float scale, std::vector<GlyphQuad>& out)
{
    layout(text, 0.0f, 0.0f, std::max(scale, 0.0f), -1.0f, out);
}

} // namespace Renderer::FontLayout
