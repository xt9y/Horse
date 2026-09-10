#include "UI/EnginePanels.hpp"

#include "Renderer/Debug/Debug.hpp"
#include "Renderer/GlobalIllumination/Debug.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace UI {
namespace {

constexpr float kUiScale = 0.82f;

float scaled(float value)
{
    return value * kUiScale;
}

ImVec2 scaled(float x, float y)
{
    return ImVec2(x * kUiScale, y * kUiScale);
}

void itemHelp(const char *text)
{
    if (!text || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 3.0f));
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(220.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

void section(const char *label)
{
    ImGui::Spacing();
    ImGui::TextUnformatted(label);
    ImGui::Separator();
}

bool checkbox(const char *label, bool *value, const char *help)
{
    const bool changed = ImGui::Checkbox(label, value);
    itemHelp(help);
    return changed;
}

bool sliderInt(
    const char *label,
    int *value,
    int minimum,
    int maximum,
    const char *help)
{
    const bool changed = ImGui::SliderInt(label, value, minimum, maximum);
    itemHelp(help);
    return changed;
}

bool sliderFloat(
    const char *label,
    float *value,
    float minimum,
    float maximum,
    const char *format,
    const char *help)
{
    const bool changed = ImGui::SliderFloat(label, value, minimum, maximum, format);
    itemHelp(help);
    return changed;
}

bool dragFloat(
    const char *label,
    float *value,
    float speed,
    float minimum,
    float maximum,
    const char *format,
    const char *help)
{
    const bool changed = ImGui::DragFloat(label, value, speed, minimum, maximum, format);
    itemHelp(help);
    return changed;
}

bool inputInt(
    const char *label,
    int *value,
    int step,
    int fast_step,
    const char *help)
{
    const bool changed = ImGui::InputInt(label, value, step, fast_step);
    itemHelp(help);
    return changed;
}

bool button(const char *label, const char *help)
{
    const bool pressed = ImGui::Button(label);
    itemHelp(help);
    return pressed;
}

bool photonMappingCombo(bool& enabled)
{
    const char *preview = enabled ? "Uniform Grid" : "Disabled";
    bool changed = false;

    const bool opened = ImGui::BeginCombo("Photon Mapping", preview);
    itemHelp("Selects whether the GI photon map is disabled or gathered through Horse's uniform spatial grid.");
    if (!opened) return false;

    if (ImGui::Selectable("Disabled", !enabled)) {
        enabled = false;
        changed = true;
    }
    itemHelp("Disables photon-map lighting.");

    if (ImGui::Selectable("Uniform Grid", enabled)) {
        enabled = true;
        changed = true;
    }
    itemHelp("Uses the uniform spatial grid to gather nearby photons.");
    if (enabled) ImGui::SetItemDefaultFocus();

    ImGui::EndCombo();
    return changed;
}

Renderer::GlobalIlluminationComponent *globalIllumination(Ecs::World& world)
{
    for (const Ecs::Entity entity : world.entities()) {
        if (Renderer::GlobalIlluminationComponent *component =
                world.get<Renderer::GlobalIlluminationComponent>(entity))
        {
            return component;
        }
    }
    return nullptr;
}

Renderer::LightComponent *firstLight(Ecs::World& world)
{
    for (const Ecs::Entity entity : world.entities()) {
        if (Renderer::LightComponent *component = world.get<Renderer::LightComponent>(entity))
            return component;
    }
    return nullptr;
}

void approximationWindow(
    Ecs::World& world,
    Renderer::RayTracer& ray_tracer,
    Renderer::PathTracer& path_tracer,
    RendererChoice renderer,
    bool& world_changed,
    bool& reset_requested)
{
    Renderer::GlobalIlluminationComponent *gi = globalIllumination(world);
    Renderer::LightComponent *light = firstLight(world);

    ImGui::SetNextWindowPos(scaled(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(390.0f, 430.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Approximation (real-time)")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(scaled(235.0f));

    section("Global Illumination");
    if (gi) {
        if (checkbox(
                "Global Illumination",
                &gi->enabled,
                "Enables or disables indirect illumination from the current GI solution."))
        {
            world_changed = true;
            if (!gi->enabled) reset_requested = true;
        }

        int bounces = static_cast<int>(gi->bounces);
        if (sliderInt(
                "Bounces",
                &bounces,
                1,
                4,
                "Changes how many indirect-light bounces are calculated."))
        {
            gi->bounces = static_cast<std::uint8_t>(bounces);
            world_changed = true;
        }

        if (sliderFloat(
                "GI Intensity",
                &gi->intensity,
                0.0f,
                4.0f,
                "%.3f",
                "Controls the visible strength of indirect illumination."))
        {
            world_changed = true;
        }

        if (photonMappingCombo(gi->photon_mapping))
            world_changed = true;

        if (gi->photon_mapping) {
            int photon_count = static_cast<int>(std::min<std::uint32_t>(gi->photon_count, 262144u));
            if (inputInt(
                    "GI Photons",
                    &photon_count,
                    1000,
                    10000,
                    "Changes how many photons are emitted into the GI map. More photons improve lighting detail but cost more to rebuild."))
            {
                gi->photon_count = static_cast<std::uint32_t>(std::clamp(photon_count, 0, 262144));
                world_changed = true;
            }

            if (dragFloat(
                    "Photon Radius",
                    &gi->photon_radius,
                    0.001f,
                    0.0f,
                    1000.0f,
                    "%.3f",
                    "Changes the world-space photon gather radius. 0 lets Horse choose it automatically."))
            {
                gi->photon_radius = std::max(gi->photon_radius, 0.0f);
                world_changed = true;
            }
        }

        if (button(
                "Rebuild GI",
                "Clears the current GI solution so it is rebuilt from the current scene and settings."))
        {
            reset_requested = true;
        }
    } else {
        ImGui::TextDisabled("No global illumination in this scene.");
    }

    section("Renderer");
    if (renderer == RendererChoice::RayTracer) {
        int divisor = std::clamp(ray_tracer.settings().resolution_divisor, 4, 8);
        if (sliderInt(
                "Resolution Divisor",
                &divisor,
                4,
                8,
                "Changes the Ray Tracer's internal render resolution. Higher values render fewer rays and run faster."))
        {
            ray_tracer.settings().resolution_divisor = divisor;
        }

        dragFloat(
            "Exposure",
            &ray_tracer.settings().exposure,
            0.01f,
            0.0f,
            32.0f,
            "%.3f",
            "Changes the brightness of the ray-traced HDR image before tone mapping."
        );
    } else if (renderer == RendererChoice::PathTracer) {
        int divisor = std::clamp(path_tracer.settings().resolution_divisor, 1, 4);
        if (sliderInt(
                "Resolution Divisor",
                &divisor,
                1,
                4,
                "Changes the Path Tracer's internal render resolution. Higher values render fewer pixels and run faster."))
        {
            path_tracer.settings().resolution_divisor = divisor;
        }

        int samples = std::clamp(path_tracer.settings().samples_per_frame, 1, 4);
        if (sliderInt(
                "Samples / Frame",
                &samples,
                1,
                4,
                "Changes how many path-tracing samples are accumulated per rendered frame."))
        {
            path_tracer.settings().samples_per_frame = samples;
        }

        dragFloat(
            "Exposure",
            &path_tracer.settings().exposure,
            0.01f,
            0.0f,
            32.0f,
            "%.3f",
            "Changes the brightness of the path-traced HDR image before tone mapping."
        );
    } else {
        ImGui::TextDisabled("No additional Rasterizer controls.");
    }

    section("Lighting");
    if (light) {
        const float speed = std::max(std::abs(light->intensity) * 0.005f, 0.01f);
        if (dragFloat(
                "Light Intensity",
                &light->intensity,
                speed,
                0.0f,
                0.0f,
                "%.3f",
                "Changes the visible intensity of the first light in the current scene."))
        {
            light->intensity = std::max(light->intensity, 0.0f);
            world_changed = true;
        }
    } else {
        ImGui::TextDisabled("No light in this scene.");
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void sceneManagerWindow(
    RendererChoice& renderer,
    const RendererAvailability& available,
    SceneSelection& scenes)
{
    const bool valid = scenes.names && scenes.count > 0u && scenes.selected < scenes.count;
    const char *preview = valid ? scenes.names[scenes.selected] : "No Scene";

    ImGui::SetNextWindowPos(scaled(410.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(250.0f, 122.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Manager")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(scaled(145.0f));
    const bool opened = ImGui::BeginCombo("Scenes", preview);
    itemHelp("Switches the live GAME scene. Each scene owns its camera, models, lighting, GI configuration and scene-specific state.");
    if (opened) {
        for (std::size_t index = 0u; index < scenes.count; ++index) {
            const bool selected = index == scenes.selected;
            if (ImGui::Selectable(scenes.names[index], selected))
                scenes.selected = index;
            itemHelp("Loads this scene at the next safe frame boundary.");
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    rendererSelector(renderer, available);
    ImGui::PopItemWidth();
    ImGui::End();
}

void informationWindow()
{
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(scaled(410.0f, 140.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(300.0f, 175.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Informations")) {
        ImGui::End();
        return;
    }

    ImGui::Text("Photons: %zu", statistics.photons);
    ImGui::Text("GI Probes: %zu", statistics.probes);
    ImGui::Text("Triangles: %zu", statistics.triangles);
    ImGui::Text("Materials: %zu", statistics.materials);
    ImGui::Separator();
    ImGui::Text("BVH Depth: %u", statistics.bvh_depth);
    ImGui::Text("BVH Build: %.3f ms", statistics.scene_build_ms);
    ImGui::Text("Photon Build: %.3f ms", statistics.photon_build_ms);
    ImGui::Text("Photon Radius: %.4f", statistics.photon_radius);

    if (statistics.calculating)
        ImGui::ProgressBar(statistics.progress, ImVec2(-1.0f, 0.0f), "GI building");

    ImGui::End();
}

void debugWindow()
{
    Renderer::Debug::Settings& debug = Renderer::Debug::settings();
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(scaled(720.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(300.0f, 250.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug View")) {
        ImGui::End();
        return;
    }

    checkbox(
        "Wireframe",
        &debug.wireframe,
        "Draws every triangle edge in the scene. The triangle under the center of the camera is highlighted yellow."
    );

    checkbox(
        "BVH",
        &debug.show_bvh,
        "Draws BVH boxes. The box containing the camera, or the nearest box under the center view ray, is highlighted yellow."
    );
    if (debug.show_bvh) {
        const int maximum_level = std::max(static_cast<int>(statistics.bvh_depth) - 1, 0);
        debug.bvh_level = std::clamp(debug.bvh_level, 0, maximum_level);
        if (maximum_level > 0) {
            sliderInt(
                "BVH Level",
                &debug.bvh_level,
                0,
                maximum_level,
                "Changes which BVH hierarchy depth is drawn."
            );
        }
    }

    checkbox(
        "Photon Map",
        &debug.show_photons,
        "Draws stored photon positions. The photon nearest the center view ray is highlighted yellow."
    );
    if (debug.show_photons) {
        sliderFloat(
            "Photon Size",
            &debug.photon_size,
            1.0f,
            5.0f,
            "%.1f",
            "Changes the visible size of photon markers."
        );
    }

    checkbox(
        "GI Probes",
        &debug.show_gi_probes,
        "Draws the GI probe grid. The probe nearest the camera is highlighted yellow."
    );
    checkbox(
        "Scene Bounds",
        &debug.show_scene_bounds,
        "Draws the scene bounds. They turn yellow while the camera is inside them."
    );
    checkbox(
        "Light",
        &debug.show_light,
        "Draws the active light. It turns yellow when the camera is near it or looking toward it."
    );
    checkbox(
        "Triangle Normals",
        &debug.show_normals,
        "Draws triangle normals. The normal on the triangle under the center view ray is highlighted yellow."
    );

    if (debug.wireframe || debug.show_bvh || debug.show_photons || debug.show_gi_probes ||
        debug.show_scene_bounds || debug.show_light || debug.show_normals)
    {
        sliderFloat(
            "Overlay Opacity",
            &debug.overlay_opacity,
            0.15f,
            1.0f,
            "%.2f",
            "Changes the visibility of active debug overlays."
        );
    }

    ImGui::End();
}

} // namespace

void enginePanels(
    Ecs::World& world,
    Renderer::RayTracer& ray_tracer,
    Renderer::PathTracer& path_tracer,
    RendererChoice& renderer,
    const RendererAvailability& available,
    SceneSelection& scenes)
{
    if (!ImGui::GetCurrentContext()) return;

    bool world_changed = false;
    bool reset_requested = false;

    approximationWindow(world, ray_tracer, path_tracer, renderer, world_changed, reset_requested);
    sceneManagerWindow(renderer, available, scenes);
    informationWindow();
    debugWindow();

    if (reset_requested) Renderer::GlobalIllumination::reset();
    if (world_changed || reset_requested) world.markChanged();
}

} // namespace UI
