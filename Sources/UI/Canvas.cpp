#include "UI/Canvas.hpp"

#include "UI/UI.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace UI::Canvas {
namespace {

ImU32 color(Renderer::Vec4 value)
{
    return IM_COL32(
        static_cast<int>(std::clamp(value.x, 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(value.y, 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(value.z, 0.0f, 1.0f) * 255.0f),
        static_cast<int>(std::clamp(value.w, 0.0f, 1.0f) * 255.0f)
    );
}

ImDrawList *drawList()
{
    if (!UI::initialized()) return nullptr;
    return ImGui::GetForegroundDrawList();
}

ImVec2 point(Renderer::Vec2 value)
{
    return {value.x, value.y};
}

} // namespace

bool begin()
{
    UI::beginFrame();
    if (!UI::initialized()) return false;
    UI::showOverlay();
    return true;
}

void filledRect(Renderer::Vec2 minimum, Renderer::Vec2 maximum, Renderer::Vec4 value)
{
    if (ImDrawList *draw = drawList())
        draw->AddRectFilled(point(minimum), point(maximum), color(value));
}

void rect(Renderer::Vec2 minimum, Renderer::Vec2 maximum, Renderer::Vec4 value, float thickness)
{
    if (ImDrawList *draw = drawList())
        draw->AddRect(point(minimum), point(maximum), color(value), 0.0f, 0, std::max(thickness, 1.0f));
}

void line(Renderer::Vec2 from, Renderer::Vec2 to, Renderer::Vec4 value, float thickness)
{
    if (ImDrawList *draw = drawList())
        draw->AddLine(point(from), point(to), color(value), std::max(thickness, 1.0f));
}

void circle(Renderer::Vec2 center, float radius, Renderer::Vec4 value, float thickness, int segments)
{
    if (ImDrawList *draw = drawList())
        draw->AddCircle(point(center), std::max(radius, 0.0f), color(value), std::max(segments, 12), std::max(thickness, 1.0f));
}

void filledCircle(Renderer::Vec2 center, float radius, Renderer::Vec4 value, int segments)
{
    if (ImDrawList *draw = drawList())
        draw->AddCircleFilled(point(center), std::max(radius, 0.0f), color(value), std::max(segments, 12));
}

void text(Renderer::Vec2 position, std::string_view value, Renderer::Vec4 tint, float scale)
{
    ImDrawList *draw = drawList();
    if (!draw || value.empty()) return;

    ImFont *font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * std::max(scale, 0.1f);
    const std::string copy(value);
    draw->AddText(font, size, point(position), color(tint), copy.c_str());
}

} // namespace UI::Canvas
