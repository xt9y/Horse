#include "UI/EnginePanels.hpp"

#include "Renderer/Debug/Debug.hpp"
#include "Renderer/GlobalIllumination/Debug.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
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

void disabledCheckbox(const char *label, bool value, const char *help)
{
    ImGui::BeginDisabled();
    bool copy = value;
    ImGui::Checkbox(label, &copy);
    itemHelp(help);
    ImGui::EndDisabled();
}

void disabledInputInt(const char *label, int value, const char *help)
{
    ImGui::BeginDisabled();
    int copy = value;
    ImGui::InputInt(label, &copy);
    itemHelp(help);
    ImGui::EndDisabled();
}

void disabledSliderInt(
    const char *label,
    int value,
    int minimum,
    int maximum,
    const char *help)
{
    ImGui::BeginDisabled();
    int copy = value;
    ImGui::SliderInt(label, &copy, minimum, maximum);
    itemHelp(help);
    ImGui::EndDisabled();
}

void disabledSliderFloat(
    const char *label,
    float value,
    float minimum,
    float maximum,
    const char *help)
{
    ImGui::BeginDisabled();
    float copy = value;
    ImGui::SliderFloat(label, &copy, minimum, maximum, "%.3f");
    itemHelp(help);
    ImGui::EndDisabled();
}

void disabledButton(const char *label, const char *help)
{
    ImGui::BeginDisabled();
    ImGui::Button(label);
    itemHelp(help);
    ImGui::EndDisabled();
}

bool dataStructureCombo(bool *enabled)
{
    const char *preview = enabled && !*enabled ? "Disabled" : "Uniform Grid";
    bool changed = false;
    const bool opened = ImGui::BeginCombo("Data Structure", preview);
    itemHelp("Selects the spatial structure used for photon gathering. Horse currently implements its uniform spatial hash grid.");
    if (!opened) return false;

    if (enabled) {
        if (ImGui::Selectable("Disabled", !*enabled)) {
            *enabled = false;
            changed = true;
        }
        itemHelp("Disables photon-map gathering while leaving the rest of GI available.");
    } else {
        ImGui::BeginDisabled();
        ImGui::Selectable("Disabled", false);
        itemHelp("No GlobalIlluminationComponent is available in this scene.");
        ImGui::EndDisabled();
    }

    const bool uniform_selected = !enabled || *enabled;
    if (ImGui::Selectable("Uniform Grid", uniform_selected) && enabled) {
        *enabled = true;
        changed = true;
    }
    itemHelp("Uses Horse's spatial hash grid to find nearby photons efficiently.");
    if (uniform_selected) ImGui::SetItemDefaultFocus();

    ImGui::BeginDisabled();
    ImGui::Selectable("KD-Tree", false);
    itemHelp("KD-tree photon storage is not implemented yet.");
    ImGui::EndDisabled();
    ImGui::EndCombo();
    return changed;
}

void toneMapperCombo(bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    const bool opened = ImGui::BeginCombo("Tonemapper", "Reinhard");
    itemHelp(enabled
        ? "Selects the HDR-to-display tone mapper. The trace renderers currently implement Reinhard."
        : "The Rasterizer does not currently expose a separate tone-mapping stage.");
    if (opened) {
        ImGui::Selectable("Reinhard", true);
        itemHelp("Maps HDR values into display range using the Reinhard curve.");
        ImGui::SetItemDefaultFocus();
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
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
    Renderer::Debug::Settings& debug = Renderer::Debug::settings();

    ImGui::SetNextWindowPos(scaled(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(430.0f, 690.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Approximation (real-time)")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(scaled(275.0f));

    section("Photon Tracing");
    if (gi) {
        int photon_count = static_cast<int>(std::min<std::uint32_t>(gi->photon_count, 262144u));
        if (inputInt(
                "GI Photons",
                &photon_count,
                1000,
                10000,
                "Number of photons emitted into the GI photon map. More photons reduce noise but increase rebuild cost and memory use."))
        {
            gi->photon_count = static_cast<std::uint32_t>(std::clamp(photon_count, 0, 262144));
            world_changed = true;
        }
    } else {
        disabledInputInt("GI Photons", 0, "This scene has no GlobalIlluminationComponent.");
    }
    disabledInputInt("Max Caustic Photons", 0, "Caustic photon tracing is not implemented yet.");

    if (gi) {
        int bounces = static_cast<int>(gi->bounces);
        if (sliderInt("Bounces", &bounces, 1, 4, "Maximum indirect-light bounce count used by the GI calculation.")) {
            gi->bounces = static_cast<std::uint8_t>(bounces);
            world_changed = true;
        }
        if (sliderFloat("GI Intensity", &gi->intensity, 0.0f, 4.0f, "%.3f", "Final multiplier applied to indirect illumination."))
            world_changed = true;
    } else {
        disabledSliderInt("Bounces", 0, 1, 4, "This scene has no GlobalIlluminationComponent.");
        disabledSliderFloat("GI Intensity", 0.0f, 0.0f, 4.0f, "This scene has no GlobalIlluminationComponent.");
    }

    int ray_divisor = std::clamp(ray_tracer.settings().resolution_divisor, 4, 8);
    if (sliderInt("Ray Trace Divisor", &ray_divisor, 4, 8, "Ray-tracing resolution divisor. Higher values trace fewer pixels and run faster."))
        ray_tracer.settings().resolution_divisor = ray_divisor;

    disabledSliderFloat("Cosine Weight", 1.0f, 0.0f, 3.0f, "The current photon integrator uses built-in cosine-weighted bounce sampling.");
    disabledSliderFloat("Scale dist. on dir. light", 1.0f, 0.0f, 2.0f, "Directional-light emission bounds are calculated automatically.");
    disabledSliderFloat("Lower Limit", 0.0f, 0.0f, 1.0f, "Reference clamp control; not exposed by the current integrator.");
    disabledSliderFloat("Upper Limit", 1.0f, 0.0f, 6.0f, "Reference clamp control; not exposed by the current integrator.");

    if (button("SHOOT PHOTONS (RESET)", "Clears the current GI/photon state so Horse rebuilds it from the current scene and settings."))
        reset_requested = true;

    section("Debugging");
    disabledCheckbox("Sampling", false, "Reference sampling visualization; not implemented yet.");
    checkbox("Show Photon Map (Points)", &debug.show_photons, "Draws stored photon positions. The photon nearest the center view ray is highlighted yellow.");

    if (gi) {
        if (checkbox("Show Global Illumination", &gi->enabled, "Enables or disables the real-time GI contribution and its background calculation.")) {
            world_changed = true;
            if (!gi->enabled) reset_requested = true;
        }
    } else {
        disabledCheckbox("Show Global Illumination", false, "This scene has no GlobalIlluminationComponent.");
    }

    disabledCheckbox("Show Caustic Illumination", false, "Caustic illumination is not implemented yet.");
    disabledCheckbox("Show Combined Illumination", false, "A separate combined-only visualization is not implemented yet.");
    disabledCheckbox("Show Full Render", true, "Horse always presents the selected renderer output.");

    section("Datastructure");
    if (gi) {
        if (dataStructureCombo(&gi->photon_mapping)) world_changed = true;
    } else {
        ImGui::BeginDisabled();
        dataStructureCombo(nullptr);
        ImGui::EndDisabled();
    }
    disabledInputInt("Grid Size", 0, "The current spatial hash grid sizes itself automatically from photon positions and gather radius.");
    disabledInputInt("Neighbor", 1, "The current gather checks surrounding hash cells automatically.");

    if (gi) {
        if (dragFloat(
                "Search Radius GI",
                &gi->photon_radius,
                0.001f,
                0.0f,
                1000.0f,
                "%.3f",
                "Photon gather radius in world units. 0 lets Horse choose it automatically from scene size and photon count."))
        {
            gi->photon_radius = std::max(gi->photon_radius, 0.0f);
            world_changed = true;
        }
    } else {
        disabledSliderFloat("Search Radius GI", 0.0f, 0.0f, 1.0f, "This scene has no GlobalIlluminationComponent.");
    }

    disabledSliderFloat("Search Radius Ca", 0.0f, 0.0f, 1.0f, "Caustic photon gathering is not implemented yet.");
    disabledCheckbox("Use Cone Filter", false, "Cone filtering is not implemented in the current photon gatherer.");
    disabledSliderFloat("Filter Constant", 1.0f, 0.0f, 2.0f, "Cone-filter constant; disabled because cone filtering is not implemented.");

    section("Tone Mapping");
    const bool trace_renderer = renderer != RendererChoice::Rasterizer;
    toneMapperCombo(trace_renderer);

    if (renderer == RendererChoice::RayTracer) {
        dragFloat("Exposure", &ray_tracer.settings().exposure, 0.01f, 0.0f, 32.0f, "%.3f", "Multiplies ray-traced HDR color before tone mapping.");
    } else if (renderer == RendererChoice::PathTracer) {
        dragFloat("Exposure", &path_tracer.settings().exposure, 0.01f, 0.0f, 32.0f, "%.3f", "Multiplies path-traced HDR color before tone mapping.");
    } else {
        disabledSliderFloat("Exposure", 1.0f, 0.0f, 4.0f, "Rasterizer exposure is not currently a separate setting.");
    }

    disabledCheckbox("Activate Gamma Correction?", true, "Trace presentation currently always applies display gamma correction.");
    disabledSliderFloat("Gamma", 2.2f, 1.0f, 5.0f, "Trace presentation currently uses a fixed gamma of 2.2.");

    if (light) {
        const float speed = std::max(std::abs(light->intensity) * 0.005f, 0.01f);
        if (dragFloat("Adjust PointBrightness", &light->intensity, speed, 0.0f, 0.0f, "%.3f", "Changes the intensity of the first scene light.")) {
            light->intensity = std::max(light->intensity, 0.0f);
            world_changed = true;
        }
    } else {
        disabledSliderFloat("Adjust PointBrightness", 0.0f, 0.0f, 1.0f, "No light exists in the current scene.");
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

void fullRenderingWindow(
    Ecs::World& world,
    Renderer::PathTracer& path_tracer,
    bool& world_changed)
{
    Renderer::GlobalIlluminationComponent *gi = globalIllumination(world);

    ImGui::SetNextWindowPos(scaled(450.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(330.0f, 350.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Full Rendering")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(scaled(190.0f));
    int divisor = std::clamp(path_tracer.settings().resolution_divisor, 1, 4);
    if (sliderInt("Resolution Divisor", &divisor, 1, 4, "Path-tracing resolution divisor. 1 is full trace resolution; larger values trade detail for speed."))
        path_tracer.settings().resolution_divisor = divisor;

    int samples = std::clamp(path_tracer.settings().samples_per_frame, 1, 4);
    if (sliderInt("Samples / Frame", &samples, 1, 4, "Number of path-tracing samples accumulated each rendered frame."))
        path_tracer.settings().samples_per_frame = samples;

    section("Uniform Grid");
    disabledInputInt("Neighbor", 1, "The current photon gather examines neighboring hash cells automatically.");

    section("Photon Gathering");
    if (gi) {
        if (dragFloat("Search Radius GI", &gi->photon_radius, 0.001f, 0.0f, 1000.0f, "%.3f", "Photon gather radius. 0 selects the automatic radius.")) {
            gi->photon_radius = std::max(gi->photon_radius, 0.0f);
            world_changed = true;
        }
    } else {
        disabledSliderFloat("Search Radius GI", 0.0f, 0.0f, 1.0f, "This scene has no GlobalIlluminationComponent.");
    }
    disabledSliderFloat("Search Radius Ca", 0.0f, 0.0f, 1.0f, "Caustic photon gathering is not implemented yet.");

    section("Which Datastructure");
    if (gi) {
        if (dataStructureCombo(&gi->photon_mapping)) world_changed = true;
    } else {
        ImGui::BeginDisabled();
        dataStructureCombo(nullptr);
        ImGui::EndDisabled();
    }

    disabledButton("RENDER", "Dedicated offline render execution is not implemented; the Path Tracer renders continuously.");
    ImGui::Separator();
    disabledButton("Save to File", "Saving the rendered framebuffer to an image is not implemented yet.");
    ImGui::Separator();
    ImGui::Text("Render time: %.6f seconds", static_cast<double>(ImGui::GetIO().DeltaTime));

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

    ImGui::SetNextWindowPos(scaled(450.0f, 368.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(250.0f, 122.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Manager")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(scaled(145.0f));
    const bool opened = ImGui::BeginCombo("Scenes", preview);
    itemHelp("Switches the live GAME scene. Each scene owns its camera, models, lighting, GI configuration and scene-specific update state.");
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

    ImGui::SetNextWindowPos(scaled(450.0f, 498.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(330.0f, 190.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Informations")) {
        ImGui::End();
        return;
    }

    ImGui::Text("# Photons: %zu", statistics.photons);
    ImGui::Text("# Global Illum: %zu", statistics.probes);
    ImGui::TextUnformatted("# Caustics: 0");
    ImGui::Separator();
    ImGui::Text("Depth of BVH-Tree: %u", statistics.bvh_depth);
    ImGui::Text("BVH build time: %.6f ms", statistics.scene_build_ms);
    ImGui::Text("Uniform Grid build time: %.6f ms", statistics.photon_build_ms);
    ImGui::Text("Triangles: %zu", statistics.triangles);
    ImGui::Text("Materials: %zu", statistics.materials);
    ImGui::Text("Photon radius: %.4f", statistics.photon_radius);
    if (statistics.calculating)
        ImGui::ProgressBar(statistics.progress, ImVec2(-1.0f, 0.0f), "GI building");

    ImGui::End();
}

void debugWindow()
{
    Renderer::Debug::Settings& debug = Renderer::Debug::settings();
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(scaled(790.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(315.0f, 285.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug View")) {
        ImGui::End();
        return;
    }

    checkbox("Wireframe", &debug.wireframe, "Draws every triangle edge in the complete trace scene. The triangle directly under the center of the camera is highlighted yellow.");
    checkbox("Show BVH", &debug.show_bvh, "Draws every BVH box at the selected level. The box containing the camera is yellow; when outside, the nearest box under the center view ray is yellow.");
    checkbox("Show Photon Map", &debug.show_photons, "Draws sampled photon markers. The photon nearest the center view ray is highlighted yellow.");
    checkbox("Show GI Probe Grid", &debug.show_gi_probes, "Draws the published GI probe grid. The probe nearest the camera is highlighted yellow.");
    checkbox("Show Scene Bounds", &debug.show_scene_bounds, "Draws the GI trace-scene bounds. The bounds turn yellow while the camera is inside them.");
    checkbox("Show Light", &debug.show_light, "Draws the active light. It turns yellow when the camera is close to it or looking directly toward it.");
    checkbox("Show Triangle Normals", &debug.show_normals, "Draws sampled triangle normals. The normal on the triangle directly under the center view ray is highlighted yellow.");

    const int maximum_level = std::max(static_cast<int>(statistics.bvh_depth) - 1, 0);
    debug.bvh_level = std::clamp(debug.bvh_level, 0, maximum_level);
    ImGui::BeginDisabled(maximum_level == 0);
    sliderInt("BVH Level", &debug.bvh_level, 0, maximum_level, "Chooses which depth of the BVH hierarchy is visualized.");
    ImGui::EndDisabled();

    sliderFloat("Overlay Opacity", &debug.overlay_opacity, 0.15f, 1.0f, "%.2f", "Brightness/opacity multiplier shared by scene debug overlays.");
    sliderFloat("Photon Point Size", &debug.photon_size, 1.0f, 5.0f, "%.1f", "World-space size multiplier used for photon markers.");

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
    fullRenderingWindow(world, path_tracer, world_changed);
    sceneManagerWindow(renderer, available, scenes);
    informationWindow();
    debugWindow();

    if (reset_requested) Renderer::GlobalIllumination::reset();
    if (world_changed || reset_requested) world.markChanged();
}

} // namespace UI
