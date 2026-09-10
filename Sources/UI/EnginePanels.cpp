#include "UI/EnginePanels.hpp"

#include "Camera.hpp"
#include "Renderer/GlobalIllumination/Debug.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <imgui.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace UI {
namespace {

constexpr float kUiScale = 0.82f;

// imgui_impl_opengl2 has no RendererHasVtxOffset support. Keep the shared
// background overlay well below the 64K-vertex limit even with every debug
// visualization enabled at once.
constexpr std::size_t kMaximumBvhBoxes = 256u;
constexpr std::size_t kMaximumPhotons = 1500u;
constexpr std::size_t kMaximumNormals = 750u;
constexpr int kMaximumWireframeTriangles = 1200;

struct DebugSettings {
    bool wireframe = false;
    bool show_bvh = false;
    bool show_photons = false;
    bool show_gi_probes = false;
    bool show_scene_bounds = false;
    bool show_light = false;
    bool show_normals = false;
    int bvh_level = 2;
    int wireframe_triangles = 800;
    float overlay_opacity = 0.80f;
    float photon_size = 2.0f;
};

struct CameraProjection {
    Renderer::Vec3 position{};
    Renderer::Vec3 forward{};
    Renderer::Vec3 right{};
    Renderer::Vec3 up{};
    float tan_half_fov = 1.0f;
    float aspect = 1.0f;
    float near_plane = 0.1f;
    float width = 1.0f;
    float height = 1.0f;
    bool valid = false;
};

DebugSettings debug_settings;

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

void section(const char *label)
{
    ImGui::Spacing();
    ImGui::TextUnformatted(label);
    ImGui::Separator();
}

bool dataStructureCombo(bool *enabled)
{
    const char *preview = enabled && !*enabled ? "Disabled" : "Uniform Grid";
    const bool opened = ImGui::BeginCombo("Data Structure", preview);
    itemHelp(
        "Selects the photon lookup structure. Horse currently implements the uniform spatial grid; KD-Tree is not implemented."
    );
    if (!opened) return false;

    bool changed = false;
    if (enabled) {
        if (ImGui::Selectable("Disabled", !*enabled)) {
            *enabled = false;
            changed = true;
        }
        itemHelp("Disables photon mapping and uses the remaining GI path.");
    } else {
        ImGui::BeginDisabled();
        ImGui::Selectable("Disabled", false);
        itemHelp("Unavailable because this scene has no GI component.");
        ImGui::EndDisabled();
    }

    const bool uniform_selected = !enabled || *enabled;
    if (ImGui::Selectable("Uniform Grid", uniform_selected) && enabled) {
        *enabled = true;
        changed = true;
    }
    itemHelp("Uses Horse's spatial hash grid to gather nearby photons.");
    if (uniform_selected) ImGui::SetItemDefaultFocus();

    ImGui::BeginDisabled();
    ImGui::Selectable("KD-Tree", false);
    itemHelp("KD-Tree photon lookup is shown for parity with the reference UI but is not implemented.");
    ImGui::EndDisabled();

    ImGui::EndCombo();
    return changed;
}

void toneMapperCombo(bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    const bool opened = ImGui::BeginCombo("Tonemapper", "Reinhard");
    itemHelp("Selects the final HDR-to-display mapping. Horse currently uses Reinhard for trace renderers.");
    if (opened) {
        ImGui::Selectable("Reinhard", true);
        itemHelp("Maps HDR values with color / (1 + color) before gamma correction.");
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

Renderer::Vec3 add(Renderer::Vec3 a, Renderer::Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Renderer::Vec3 subtract(Renderer::Vec3 a, Renderer::Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Renderer::Vec3 multiply(Renderer::Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

CameraProjection cameraProjection(const Ecs::World& world)
{
    CameraProjection result;
    const Renderer::Scenes::Scene::CameraState camera =
        Renderer::Scenes::Scene::cameraState(world);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (!camera.valid || display.x <= 1.0f || display.y <= 1.0f) return result;

    result.position = camera.transform.position;
    result.forward = Renderer::Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    result.right = Renderer::Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    result.up = Renderer::Math::normalize(Renderer::Math::cross(result.right, result.forward));
    result.tan_half_fov = std::tan(
        std::clamp(camera.fov_degrees, 1.0f, 179.0f) * 0.008726646259971648f
    );
    result.aspect = display.x / display.y;
    result.near_plane = std::max(camera.near_plane, 1.0e-4f);
    result.width = display.x;
    result.height = display.y;
    result.valid = result.tan_half_fov > 1.0e-6f;
    return result;
}

bool projectPoint(const CameraProjection& camera, Renderer::Vec3 point, ImVec2& out)
{
    if (!camera.valid) return false;
    const Renderer::Vec3 relative = subtract(point, camera.position);
    const float depth = Renderer::Math::dot(relative, camera.forward);
    if (depth <= camera.near_plane) return false;

    const float x = Renderer::Math::dot(relative, camera.right) /
        (depth * camera.tan_half_fov * camera.aspect);
    const float y = Renderer::Math::dot(relative, camera.up) /
        (depth * camera.tan_half_fov);

    out.x = (x * 0.5f + 0.5f) * camera.width;
    out.y = (0.5f - y * 0.5f) * camera.height;
    return out.x > -camera.width && out.x < camera.width * 2.0f &&
        out.y > -camera.height && out.y < camera.height * 2.0f;
}

void drawSegment(
    ImDrawList *draw_list,
    const CameraProjection& camera,
    Renderer::Vec3 a,
    Renderer::Vec3 b,
    ImU32 color,
    float thickness = 1.0f)
{
    ImVec2 screen_a{};
    ImVec2 screen_b{};
    if (!projectPoint(camera, a, screen_a) || !projectPoint(camera, b, screen_b)) return;
    draw_list->AddLine(screen_a, screen_b, color, thickness);
}

void drawAabb(
    ImDrawList *draw_list,
    const CameraProjection& camera,
    Renderer::Vec3 minimum,
    Renderer::Vec3 maximum,
    ImU32 color,
    float thickness = 1.0f)
{
    const std::array<Renderer::Vec3, 8> corners {{
        {minimum.x, minimum.y, minimum.z},
        {maximum.x, minimum.y, minimum.z},
        {maximum.x, maximum.y, minimum.z},
        {minimum.x, maximum.y, minimum.z},
        {minimum.x, minimum.y, maximum.z},
        {maximum.x, minimum.y, maximum.z},
        {maximum.x, maximum.y, maximum.z},
        {minimum.x, maximum.y, maximum.z},
    }};
    static constexpr std::array<std::array<int, 2>, 12> edges {{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};

    for (const auto& edge : edges)
        drawSegment(draw_list, camera, corners[edge[0]], corners[edge[1]], color, thickness);
}

Renderer::Vec3 nodeMinimum(const Renderer::Scenes::GpuNode& node)
{
    return {node.min_x, node.min_y, node.min_z};
}

Renderer::Vec3 nodeMaximum(const Renderer::Scenes::GpuNode& node)
{
    return {node.max_x, node.max_y, node.max_z};
}

void drawBvhLevel(
    const std::vector<Renderer::Scenes::GpuNode>& nodes,
    std::uint32_t index,
    int depth,
    int target_depth,
    ImDrawList *draw_list,
    const CameraProjection& camera,
    ImU32 color,
    std::size_t& drawn)
{
    if (index >= nodes.size() || drawn >= kMaximumBvhBoxes) return;
    const Renderer::Scenes::GpuNode& node = nodes[index];
    const bool leaf = (node.meta & Renderer::Scenes::LeafBit) != 0u;

    if (depth == target_depth) {
        drawAabb(draw_list, camera, nodeMinimum(node), nodeMaximum(node), color, 1.0f);
        ++drawn;
        return;
    }
    if (leaf || depth > target_depth) return;

    drawBvhLevel(nodes, node.first, depth + 1, target_depth, draw_list, camera, color, drawn);
    drawBvhLevel(nodes, node.meta, depth + 1, target_depth, draw_list, camera, color, drawn);
}

Renderer::Vec3 trianglePosition(const std::array<float, 4>& value)
{
    return {value[0], value[1], value[2]};
}

void drawWireframe(
    ImDrawList *draw_list,
    const CameraProjection& camera,
    const std::vector<Renderer::Scenes::GpuTriangle>& triangles,
    ImU32 color)
{
    if (triangles.empty()) return;

    const std::size_t limit = static_cast<std::size_t>(std::clamp(
        debug_settings.wireframe_triangles,
        100,
        kMaximumWireframeTriangles
    ));
    const std::size_t stride = std::max<std::size_t>(
        1u,
        (triangles.size() + limit - 1u) / limit
    );

    std::size_t drawn = 0u;
    for (std::size_t index = 0u; index < triangles.size() && drawn < limit; index += stride, ++drawn) {
        const Renderer::Scenes::GpuTriangle& triangle = triangles[index];
        const Renderer::Vec3 p0 = trianglePosition(triangle.p0);
        const Renderer::Vec3 p1 = trianglePosition(triangle.p1);
        const Renderer::Vec3 p2 = trianglePosition(triangle.p2);
        drawSegment(draw_list, camera, p0, p1, color, 1.0f);
        drawSegment(draw_list, camera, p1, p2, color, 1.0f);
        drawSegment(draw_list, camera, p2, p0, color, 1.0f);
    }
}

void drawNormals(
    ImDrawList *draw_list,
    const CameraProjection& camera,
    const std::vector<Renderer::Scenes::GpuTriangle>& triangles,
    const Renderer::GlobalIllumination::TraceBounds& bounds,
    ImU32 color)
{
    if (triangles.empty() || !bounds.valid) return;

    const Renderer::Vec3 extent = subtract(bounds.maximum, bounds.minimum);
    const float length = std::max({extent.x, extent.y, extent.z, 1.0e-3f}) * 0.015f;
    const std::size_t stride = std::max<std::size_t>(
        1u,
        (triangles.size() + kMaximumNormals - 1u) / kMaximumNormals
    );

    std::size_t drawn = 0u;
    for (std::size_t index = 0u;
         index < triangles.size() && drawn < kMaximumNormals;
         index += stride, ++drawn)
    {
        const Renderer::Scenes::GpuTriangle& triangle = triangles[index];
        const Renderer::Vec3 p0 = trianglePosition(triangle.p0);
        const Renderer::Vec3 p1 = trianglePosition(triangle.p1);
        const Renderer::Vec3 p2 = trianglePosition(triangle.p2);
        const Renderer::Vec3 center = multiply(add(add(p0, p1), p2), 1.0f / 3.0f);
        Renderer::Vec3 normal {
            triangle.n0[0] + triangle.n1[0] + triangle.n2[0],
            triangle.n0[1] + triangle.n1[1] + triangle.n2[1],
            triangle.n0[2] + triangle.n1[2] + triangle.n2[2],
        };
        normal = Renderer::Math::normalize(normal);
        drawSegment(draw_list, camera, center, add(center, multiply(normal, length)), color, 1.0f);
    }
}

Renderer::Vec3 probePosition(
    const Renderer::GlobalIllumination::Field& field,
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t z)
{
    const auto axis = [](float minimum, float maximum, std::uint32_t coordinate, std::uint32_t size) {
        if (size <= 1u) return minimum;
        return minimum + (maximum - minimum) *
            (static_cast<float>(coordinate) / static_cast<float>(size - 1u));
    };
    return {
        axis(field.minimum.x, field.maximum.x, x, field.size_x),
        axis(field.minimum.y, field.maximum.y, y, field.size_y),
        axis(field.minimum.z, field.maximum.z, z, field.size_z),
    };
}

bool hasDebugOverlay()
{
    return debug_settings.wireframe || debug_settings.show_bvh ||
        debug_settings.show_photons || debug_settings.show_gi_probes ||
        debug_settings.show_scene_bounds || debug_settings.show_light ||
        debug_settings.show_normals;
}

bool drawOverlays(const Ecs::World& world)
{
    if (!hasDebugOverlay()) return false;

    const CameraProjection camera = cameraProjection(world);
    if (!camera.valid) return false;

    const int alpha = std::clamp(
        static_cast<int>(debug_settings.overlay_opacity * 255.0f),
        0,
        255
    );

    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();
    if (!draw_list) return false;

    const ImDrawListFlags previous_flags = draw_list->Flags;
    draw_list->Flags &= ~ImDrawListFlags_AntiAliasedLines;

    const Renderer::GlobalIllumination::TraceScene& trace_scene =
        Renderer::GlobalIllumination::Debug::traceScene();
    const Renderer::Scenes::SceneCache& cache = trace_scene.cache();
    const Renderer::GlobalIllumination::TraceBounds bounds = trace_scene.bounds();

    if (debug_settings.wireframe) {
        drawWireframe(
            draw_list,
            camera,
            cache.triangles(),
            IM_COL32(112, 170, 255, std::max(alpha / 2, 48))
        );
    }

    if (debug_settings.show_bvh && !cache.nodes().empty()) {
        const Renderer::GlobalIllumination::Debug::Statistics statistics =
            Renderer::GlobalIllumination::Debug::statistics();
        const int maximum_level = std::max(static_cast<int>(statistics.bvh_depth) - 1, 0);
        debug_settings.bvh_level = std::clamp(debug_settings.bvh_level, 0, maximum_level);
        std::size_t drawn = 0u;
        drawBvhLevel(
            cache.nodes(),
            0u,
            0,
            debug_settings.bvh_level,
            draw_list,
            camera,
            IM_COL32(72, 138, 230, alpha),
            drawn
        );
    }

    if (debug_settings.show_photons) {
        const Renderer::GlobalIllumination::PhotonMapping::PhotonMap& map =
            Renderer::GlobalIllumination::Debug::photonMap();
        const Renderer::GlobalIllumination::PhotonMapping::Photon *photons = map.photonData();
        const std::size_t photon_count = map.photonCount();
        if (photons && photon_count > 0u) {
            const std::size_t stride = std::max<std::size_t>(
                1u,
                (photon_count + kMaximumPhotons - 1u) / kMaximumPhotons
            );
            const float size = std::clamp(debug_settings.photon_size, 1.0f, 5.0f);
            std::size_t drawn = 0u;
            for (std::size_t index = 0u;
                 index < photon_count && drawn < kMaximumPhotons;
                 index += stride, ++drawn)
            {
                ImVec2 point{};
                if (!projectPoint(camera, photons[index].position, point)) continue;
                draw_list->AddRectFilled(
                    ImVec2(point.x - size, point.y - size),
                    ImVec2(point.x + size, point.y + size),
                    IM_COL32(86, 161, 255, alpha)
                );
            }
        }
    }

    if (debug_settings.show_gi_probes) {
        const Renderer::GlobalIllumination::Field *field =
            Renderer::GlobalIllumination::Debug::field();
        if (field && field->valid()) {
            for (std::uint32_t z = 0u; z < field->size_z; ++z) {
                for (std::uint32_t y = 0u; y < field->size_y; ++y) {
                    for (std::uint32_t x = 0u; x < field->size_x; ++x) {
                        ImVec2 point{};
                        if (!projectPoint(camera, probePosition(*field, x, y, z), point)) continue;
                        draw_list->AddRect(
                            ImVec2(point.x - 2.5f, point.y - 2.5f),
                            ImVec2(point.x + 2.5f, point.y + 2.5f),
                            IM_COL32(116, 205, 255, alpha),
                            0.0f,
                            0,
                            1.0f
                        );
                    }
                }
            }
        }
    }

    if (debug_settings.show_scene_bounds && bounds.valid) {
        drawAabb(
            draw_list,
            camera,
            bounds.minimum,
            bounds.maximum,
            IM_COL32(190, 213, 255, alpha),
            1.5f
        );
    }

    if (debug_settings.show_normals) {
        drawNormals(
            draw_list,
            camera,
            cache.triangles(),
            bounds,
            IM_COL32(120, 225, 190, alpha)
        );
    }

    if (debug_settings.show_light) {
        const Renderer::Scenes::LightState light = Renderer::Scenes::lightState(
            Renderer::Scenes::Scene::lightState(world)
        );
        if (light.valid) {
            ImVec2 point{};
            if (projectPoint(camera, light.position, point)) {
                draw_list->AddCircle(point, 7.0f, IM_COL32(255, 226, 128, alpha), 12, 1.0f);
                draw_list->AddLine(
                    ImVec2(point.x - 9.0f, point.y),
                    ImVec2(point.x + 9.0f, point.y),
                    IM_COL32(255, 226, 128, alpha),
                    1.0f
                );
                draw_list->AddLine(
                    ImVec2(point.x, point.y - 9.0f),
                    ImVec2(point.x, point.y + 9.0f),
                    IM_COL32(255, 226, 128, alpha),
                    1.0f
                );
            }
            if (light.type == Renderer::LightType::Directional && bounds.valid) {
                const Renderer::Vec3 extent = subtract(bounds.maximum, bounds.minimum);
                const float length = std::max({extent.x, extent.y, extent.z, 1.0f}) * 0.15f;
                drawSegment(
                    draw_list,
                    camera,
                    light.position,
                    add(light.position, multiply(light.direction, length)),
                    IM_COL32(255, 226, 128, alpha),
                    1.5f
                );
            }
        }
    }

    draw_list->Flags = previous_flags;
    return true;
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
                "Number of photons emitted when the photon map is rebuilt. More photons improve density but increase rebuild cost."))
        {
            gi->photon_count = static_cast<std::uint32_t>(std::clamp(photon_count, 0, 262144));
            world_changed = true;
        }
    } else {
        disabledInputInt("GI Photons", 0, "This scene has no GlobalIlluminationComponent.");
    }

    disabledInputInt(
        "Max Caustic Photons",
        0,
        "Caustic photon tracing is not implemented yet."
    );

    if (gi) {
        int bounces = static_cast<int>(gi->bounces);
        if (sliderInt(
                "Bounces",
                &bounces,
                1,
                4,
                "Maximum indirect-light bounce count used by the GI calculation."))
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
                "Final multiplier applied to indirect illumination."))
        {
            world_changed = true;
        }
    } else {
        disabledSliderInt("Bounces", 0, 1, 4, "This scene has no GlobalIlluminationComponent.");
        disabledSliderFloat("GI Intensity", 0.0f, 0.0f, 4.0f, "This scene has no GlobalIlluminationComponent.");
    }

    int ray_divisor = std::clamp(ray_tracer.settings().resolution_divisor, 4, 8);
    if (sliderInt(
            "Ray Trace Divisor",
            &ray_divisor,
            4,
            8,
            "Ray-tracing resolution divisor. Higher values trace fewer pixels and run faster."))
    {
        ray_tracer.settings().resolution_divisor = ray_divisor;
    }

    disabledSliderFloat("Cosine Weight", 1.0f, 0.0f, 3.0f, "Reference option; the current photon integrator uses its built-in cosine-weighted bounce sampling.");
    disabledSliderFloat("Scale dist. on dir. light", 1.0f, 0.0f, 2.0f, "Reference option; directional-light emission bounds are calculated automatically.");
    disabledSliderFloat("Lower Limit", 0.0f, 0.0f, 1.0f, "Reference clamp control; not exposed by the current integrator.");
    disabledSliderFloat("Upper Limit", 1.0f, 0.0f, 6.0f, "Reference clamp control; not exposed by the current integrator.");

    if (button(
            "SHOOT PHOTONS (RESET)",
            "Clears the current GI/photon state so Horse rebuilds it from the current scene and settings."))
    {
        reset_requested = true;
    }

    section("Debugging");
    disabledCheckbox("Sampling", false, "Reference sampling visualization; not implemented yet.");
    checkbox(
        "Show Photon Map (Points)",
        &debug_settings.show_photons,
        "Draws a sampled set of stored photons as blue quads over the scene. The overlay stays visible when the mouse is grabbed again."
    );

    if (gi) {
        if (checkbox(
                "Show Global Illumination",
                &gi->enabled,
                "Enables or disables the real-time GI contribution and its background calculation."))
        {
            world_changed = true;
            if (!gi->enabled) reset_requested = true;
        }
    } else {
        disabledCheckbox("Show Global Illumination", false, "This scene has no GlobalIlluminationComponent.");
    }

    disabledCheckbox("Show Caustic Illumination", false, "Caustic illumination is not implemented yet.");
    disabledCheckbox("Show Combined Illumination", false, "Separate combined-only visualization is not implemented yet.");
    disabledCheckbox("Show Full Render", true, "Reference display toggle; Horse always presents the selected renderer output.");

    section("Datastructure");
    if (gi) {
        if (dataStructureCombo(&gi->photon_mapping)) world_changed = true;
    } else {
        ImGui::BeginDisabled();
        dataStructureCombo(nullptr);
        ImGui::EndDisabled();
    }

    disabledInputInt("Grid Size", 0, "The current spatial hash grid sizes itself from photon positions and gather radius.");
    disabledInputInt("Neighbor", 1, "The current gather checks the surrounding 3x3x3 hash cells automatically.");

    if (gi) {
        if (dragFloat(
                "Search Radius GI",
                &gi->photon_radius,
                0.001f,
                0.0f,
                1000.0f,
                "%.3f",
                "Photon gather radius in world units. Set to 0 to let Horse choose it automatically from scene size and photon count."))
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
        dragFloat(
            "Exposure",
            &ray_tracer.settings().exposure,
            0.01f,
            0.0f,
            32.0f,
            "%.3f",
            "Multiplies ray-traced HDR color before tone mapping."
        );
    } else if (renderer == RendererChoice::PathTracer) {
        dragFloat(
            "Exposure",
            &path_tracer.settings().exposure,
            0.01f,
            0.0f,
            32.0f,
            "%.3f",
            "Multiplies path-traced HDR color before tone mapping."
        );
    } else {
        disabledSliderFloat("Exposure", 1.0f, 0.0f, 4.0f, "Rasterizer exposure is not currently a separate setting.");
    }

    disabledCheckbox("Activate Gamma Correction?", true, "Trace presentation currently always applies display gamma correction.");
    disabledSliderFloat("Gamma", 2.2f, 1.0f, 5.0f, "Trace presentation currently uses a fixed gamma of 2.2.");

    if (light) {
        const float speed = std::max(std::abs(light->intensity) * 0.005f, 0.01f);
        if (dragFloat(
                "Adjust PointBrightness",
                &light->intensity,
                speed,
                0.0f,
                0.0f,
                "%.3f",
                "Changes the intensity of the first scene light. GI and direct lighting will rebuild/update from this value."))
        {
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
    if (sliderInt(
            "Resolution Divisor",
            &divisor,
            1,
            4,
            "Path-tracing resolution divisor. 1 is full trace resolution; larger values trade detail for speed."))
    {
        path_tracer.settings().resolution_divisor = divisor;
    }

    int samples = std::clamp(path_tracer.settings().samples_per_frame, 1, 4);
    if (sliderInt(
            "Samples / Frame",
            &samples,
            1,
            4,
            "Number of path-tracing samples accumulated each rendered frame."))
    {
        path_tracer.settings().samples_per_frame = samples;
    }

    section("Uniform Grid");
    disabledInputInt("Neighbor", 1, "The current photon gather examines neighboring hash cells automatically.");

    section("Photon Gathering");
    if (gi) {
        if (dragFloat(
                "Search Radius GI",
                &gi->photon_radius,
                0.001f,
                0.0f,
                1000.0f,
                "%.3f",
                "Photon gather radius. 0 selects the automatic radius."))
        {
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
    const char *scene_name)
{
    const char *name = scene_name && scene_name[0] != '\0' ? scene_name : "Scene";

    ImGui::SetNextWindowPos(scaled(450.0f, 368.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(250.0f, 142.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Manager")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(scaled(145.0f));
    const bool opened = ImGui::BeginCombo("Scenes", name);
    itemHelp("Shows the currently loaded scene. Runtime scene switching is not implemented in these examples yet.");
    if (opened) {
        ImGui::Selectable(name, true);
        itemHelp("The scene currently loaded by GAME.");
        ImGui::SetItemDefaultFocus();
        ImGui::EndCombo();
    }

    disabledButton("Load Scene", "Runtime scene loading from this window is not implemented yet.");
    ImGui::Separator();
    rendererSelector(renderer, available);
    ImGui::PopItemWidth();
    ImGui::End();
}

void informationWindow()
{
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(scaled(450.0f, 518.0f), ImGuiCond_FirstUseEver);
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
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(scaled(790.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(scaled(315.0f, 315.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug View")) {
        ImGui::End();
        return;
    }

    checkbox(
        "Wireframe",
        &debug_settings.wireframe,
        "Draws a sampled triangle-edge overlay. The overlay is budgeted to stay safe on the OpenGL2 ImGui backend and remains visible after the mouse is grabbed."
    );
    checkbox("Show BVH", &debug_settings.show_bvh, "Draws BVH node bounds at the selected tree level.");
    checkbox("Show Photon Map", &debug_settings.show_photons, "Draws stored photon positions as small blue quads.");
    checkbox("Show GI Probe Grid", &debug_settings.show_gi_probes, "Draws the positions of the currently published GI probes.");
    checkbox("Show Scene Bounds", &debug_settings.show_scene_bounds, "Draws the world-space bounds used by the GI trace scene.");
    checkbox("Show Light", &debug_settings.show_light, "Draws the active light position and directional vector when applicable.");
    checkbox("Show Triangle Normals", &debug_settings.show_normals, "Draws a sampled set of geometric normal vectors over the trace scene.");

    const int maximum_level = std::max(static_cast<int>(statistics.bvh_depth) - 1, 0);
    debug_settings.bvh_level = std::clamp(debug_settings.bvh_level, 0, maximum_level);
    ImGui::BeginDisabled(maximum_level == 0);
    sliderInt(
        "BVH Level",
        &debug_settings.bvh_level,
        0,
        maximum_level,
        "Chooses which depth of the BVH hierarchy is visualized."
    );
    ImGui::EndDisabled();

    sliderInt(
        "Wireframe Triangles",
        &debug_settings.wireframe_triangles,
        100,
        kMaximumWireframeTriangles,
        "Maximum number of sampled triangles drawn by the wireframe overlay. It is capped to keep ImGui's OpenGL2 draw list below its 64K-vertex limit."
    );
    sliderFloat(
        "Overlay Opacity",
        &debug_settings.overlay_opacity,
        0.15f,
        1.0f,
        "%.2f",
        "Opacity shared by all scene debug overlays."
    );
    sliderFloat(
        "Photon Point Size",
        &debug_settings.photon_size,
        1.0f,
        5.0f,
        "%.1f",
        "Screen-space half-size of each photon marker in pixels."
    );

    ImGui::End();
}

} // namespace

void enginePanels(
    Ecs::World& world,
    Renderer::RayTracer& ray_tracer,
    Renderer::PathTracer& path_tracer,
    RendererChoice& renderer,
    const RendererAvailability& available,
    const char *scene_name)
{
    if (!ImGui::GetCurrentContext() || Mouse.isGrabbed() != LWCGL_FALSE) return;

    bool world_changed = false;
    bool reset_requested = false;

    approximationWindow(
        world,
        ray_tracer,
        path_tracer,
        renderer,
        world_changed,
        reset_requested
    );
    fullRenderingWindow(world, path_tracer, world_changed);
    sceneManagerWindow(renderer, available, scene_name);
    informationWindow();
    debugWindow();

    if (reset_requested) Renderer::GlobalIllumination::reset();
    if (world_changed || reset_requested) world.markChanged();
}

namespace Internal {

bool renderPersistentOverlays(const Ecs::World& world)
{
    if (!ImGui::GetCurrentContext()) return false;
    return drawOverlays(world);
}

} // namespace Internal
} // namespace UI
