#include "UI/RendererSelector.hpp"

#include <imgui.h>

namespace UI {
namespace {

bool option(
    const char *label,
    RendererChoice value,
    RendererChoice& choice,
    bool available)
{
    ImGui::BeginDisabled(!available);
    const bool selected = choice == value;
    const bool pressed = ImGui::Selectable(label, selected);
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
    if (ImGui::BeginCombo("Renderer Backend", rendererName(choice))) {
        changed |= option("Rasterizer", RendererChoice::Rasterizer, choice, available.rasterizer);
        changed |= option("Ray Tracer", RendererChoice::RayTracer, choice, available.ray_tracer);
        changed |= option("Path Tracer", RendererChoice::PathTracer, choice, available.path_tracer);
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
