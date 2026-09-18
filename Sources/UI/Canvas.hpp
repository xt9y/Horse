#ifndef HORSE_UI_CANVAS_HPP
#define HORSE_UI_CANVAS_HPP

#include "Renderer/Components.hpp"

#include <string_view>

namespace UI::Canvas {

bool begin();

void filledRect(Renderer::Vec2 minimum, Renderer::Vec2 maximum, Renderer::Vec4 color);
void rect(Renderer::Vec2 minimum, Renderer::Vec2 maximum, Renderer::Vec4 color, float thickness = 1.0f);
void line(Renderer::Vec2 from, Renderer::Vec2 to, Renderer::Vec4 color, float thickness = 1.0f);
void circle(Renderer::Vec2 center, float radius, Renderer::Vec4 color, float thickness = 1.0f, int segments = 64);
void filledCircle(Renderer::Vec2 center, float radius, Renderer::Vec4 color, int segments = 32);
void text(Renderer::Vec2 position, std::string_view value, Renderer::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, float scale = 1.0f);

} // namespace UI::Canvas

#endif
