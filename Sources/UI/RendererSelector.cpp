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
    const bool pressed = ImGui::RadioButton(label, selected);
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

    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(180.0f, 0.0f), ImGuiCond_FirstUseEver);

    bool changed = false;
    if (ImGui::Begin("Renderer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        changed |= option("Rasterizer", RendererChoice::Rasterizer, choice, available.rasterizer);
        changed |= option("Ray Tracer", RendererChoice::RayTracer, choice, available.ray_tracer);
        changed |= option("Path Tracer", RendererChoice::PathTracer, choice, available.path_tracer);
    }
    ImGui::End();
    return changed;
}

const char *rendererName(RendererChoice choice)
{
    switch (choice) {
        case RendererChoice::Rasterizer: return "Rasterizer";
        case RendererChoice::RayTracer: return "RayTracer";
        case RendererChoice::PathTracer: return "PathTracer";
    }
    return "Rasterizer";
}

} // namespace UI
