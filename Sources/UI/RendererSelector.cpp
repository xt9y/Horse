#include "UI/RendererSelector.hpp"

#include <imgui.h>

namespace UI {
namespace {

void itemHelp(const char *text)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 3.0f));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(220.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

bool option(
    const char *label,
    RendererChoice value,
    RendererChoice& choice,
    bool available,
    const char *help)
{
    ImGui::BeginDisabled(!available);
    const bool selected = choice == value;
    const bool pressed = ImGui::Selectable(label, selected);
    itemHelp(available ? help : "This renderer backend failed to initialize and cannot be selected.");
    if (selected) ImGui::SetItemDefaultFocus();
    ImGui::EndDisabled();

    if (!pressed || !available) return false;
    choice = value;
    return true;
}

} // namespace

bool rendererSelector(
    RendererChoice& choice,
    const RendererAvailability& available)
{
    if (!ImGui::GetCurrentContext()) return false;

    bool changed = false;
    const bool opened = ImGui::BeginCombo("Renderer Backend", rendererName(choice));
    itemHelp("Selects which initialized Horse renderer draws the scene.");
    if (opened) {
        changed |= option(
            "Rasterizer",
            RendererChoice::Rasterizer,
            choice,
            available.rasterizer,
            "Uses Horse's real-time OpenGL rasterizer."
        );
        changed |= option(
            "Ray Tracer",
            RendererChoice::RayTracer,
            choice,
            available.ray_tracer,
            "Uses the real-time ray tracer; on Apple this uses native Metal ray tracing."
        );
        changed |= option(
            "Path Tracer",
            RendererChoice::PathTracer,
            choice,
            available.path_tracer,
            "Uses the progressive path tracer for higher-quality accumulated rendering."
        );
        ImGui::EndCombo();
    }
    return changed;
}

const char *rendererName(RendererChoice choice)
{
    switch (choice) {
        case RendererChoice::Rasterizer: return "Rasterizer";
        case RendererChoice::RayTracer: return "Ray Tracer";
        case RendererChoice::PathTracer: return "Path Tracer";
    }
    return "Rasterizer";
}

} // namespace UI
