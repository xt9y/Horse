#include "UI/EnginePanels.hpp"

#include "Camera.hpp"
#include "Renderer/GlobalIllumination/Debug.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace UI {
namespace {

struct DebugSettings {
    bool wireframe = false;
    bool show_bvh = false;
    bool show_photons = false;
    bool show_gi_probes = false;
    bool show_scene_bounds = false;
    bool show_light = false;
    bool show_normals = false;
    int bvh_level = 2;
    int wireframe_triangles = 20000;
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

void section(const char *label)
{
    ImGui::Spacing();
    ImGui::TextUnformatted(label);
    ImGui::Separator();
}

void disabledCheckbox(const char *label, bool value)
{
    ImGui::BeginDisabled();
    bool copy = value;
    ImGui::Checkbox(label, &copy);
    ImGui::EndDisabled();
}

void disabledInputInt(const char *label, int value)
{
    ImGui::BeginDisabled();
    int copy = value;
    ImGui::InputInt(label, &copy);
    ImGui::EndDisabled();
}

void disabledSliderFloat(const char *label, float value, float minimum, float maximum)
{
    ImGui::BeginDisabled();
    float copy = value;
    ImGui::SliderFloat(label, &copy, minimum, maximum, "%.3f");
    ImGui::EndDisabled();
}

void dataStructureCombo()
{
    if (!ImGui::BeginCombo("Data Structure", "Uniform Grid")) return;

    ImGui::Selectable("Uniform Grid", true);
    ImGui::SetItemDefaultFocus();
    ImGui::BeginDisabled();
    ImGui::Selectable("KD-Tree", false);
    ImGui::EndDisabled();
    ImGui::EndCombo();
}

void toneMapperCombo(bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    if (ImGui::BeginCombo("Tonemapper", "Reinhard")) {
        ImGui::Selectable("Reinhard", true);
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
    if (index >= nodes.size() || drawn >= 4096u) return;
    const Renderer::Scenes::GpuNode& node = nodes[index];
    const bool leaf = (node.meta & Renderer::Scenes::LeafBit) != 0u;

    if (depth == target_depth) {
        drawAabb(draw_list, camera, nodeMinimum(node), nodeMaximum(node), color, 1.25f);
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
    const std::size_t limit = static_cast<std::size_t>(
        std::max(debug_settings.wireframe_triangles, 1000)
    );
    const std::size_t stride = std::max<std::size_t>(
        1u,
        (triangles.size() + limit - 1u) / limit
    );

    for (std::size_t index = 0u; index < triangles.size(); index += stride) {
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
    const std::size_t limit = 2500u;
    const std::size_t stride = std::max<std::size_t>(
        1u,
        (triangles.size() + limit - 1u) / limit
    );

    for (std::size_t index = 0u; index < triangles.size(); index += stride) {
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

void drawOverlays(Ecs::World& world)
{
    if (!debug_settings.wireframe && !debug_settings.show_bvh &&
        !debug_settings.show_photons && !debug_settings.show_gi_probes &&
        !debug_settings.show_scene_bounds && !debug_settings.show_light &&
        !debug_settings.show_normals)
    {
        return;
    }

    const CameraProjection camera = cameraProjection(world);
    if (!camera.valid) return;

    const int alpha = std::clamp(
        static_cast<int>(debug_settings.overlay_opacity * 255.0f),
        0,
        255
    );
    ImDrawList *draw_list = ImGui::GetBackgroundDrawList();
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
            const std::size_t limit = 6000u;
            const std::size_t stride = std::max<std::size_t>(
                1u,
                (photon_count + limit - 1u) / limit
            );
            const float size = std::clamp(debug_settings.photon_size, 1.0f, 5.0f);
            for (std::size_t index = 0u; index < photon_count; index += stride) {
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
                            1.25f
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
            2.0f
        );
    }

    if (debug_settings.show_normals)
        drawNormals(draw_list, camera, cache.triangles(), bounds, IM_COL32(120, 225, 190, alpha));

    if (debug_settings.show_light) {
        const Renderer::Scenes::LightState light = Renderer::Scenes::lightState(
            Renderer::Scenes::Scene::lightState(world)
        );
        if (light.valid) {
            ImVec2 point{};
            if (projectPoint(camera, light.position, point)) {
                draw_list->AddCircle(point, 7.0f, IM_COL32(255, 226, 128, alpha), 12, 1.5f);
                draw_list->AddLine(
                    ImVec2(point.x - 10.0f, point.y),
                    ImVec2(point.x + 10.0f, point.y),
                    IM_COL32(255, 226, 128, alpha),
                    1.0f
                );
                draw_list->AddLine(
                    ImVec2(point.x, point.y - 10.0f),
                    ImVec2(point.x, point.y + 10.0f),
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
                    2.0f
                );
            }
        }
    }
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

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 690.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Approximation (real-time)")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(275.0f);

    section("Photon Tracing");
    if (gi) {
        int photon_count = static_cast<int>(std::min<std::uint32_t>(gi->photon_count, 262144u));
        if (ImGui::InputInt("GI Photons", &photon_count, 1000, 10000)) {
            gi->photon_count = static_cast<std::uint32_t>(std::clamp(photon_count, 0, 262144));
            world_changed = true;
        }
    } else {
        disabledInputInt("GI Photons", 0);
    }
    disabledInputInt("Max Caustic Photons", 0);

    if (gi) {
        int bounces = static_cast<int>(gi->bounces);
        if (ImGui::SliderInt("Bounces", &bounces, 1, 4)) {
            gi->bounces = static_cast<std::uint8_t>(bounces);
            world_changed = true;
        }
        if (ImGui::SliderFloat("GI Intensity", &gi->intensity, 0.0f, 4.0f, "%.3f"))
            world_changed = true;
    } else {
        disabledSliderFloat("Bounces", 0.0f, 0.0f, 4.0f);
        disabledSliderFloat("GI Intensity", 0.0f, 0.0f, 4.0f);
    }

    disabledSliderFloat("Cosine Weight", 1.0f, 0.0f, 3.0f);
    disabledSliderFloat("Scale dist. on dir. light", 1.0f, 0.0f, 2.0f);
    disabledSliderFloat("Lower Limit", 0.0f, 0.0f, 1.0f);
    disabledSliderFloat("Upper Limit", 1.0f, 0.0f, 6.0f);
    if (ImGui::Button("SHOOT PHOTONS (RESET)")) reset_requested = true;

    section("Debugging");
    disabledCheckbox("Sampling", false);
    ImGui::Checkbox("Show Photon Map (Points)", &debug_settings.show_photons);
    if (gi) {
        if (ImGui::Checkbox("Show Global Illumination", &gi->enabled)) world_changed = true;
    } else {
        disabledCheckbox("Show Global Illumination", false);
    }
    disabledCheckbox("Show Caustic Illumination", false);
    disabledCheckbox("Show Combined Illumination", false);
    disabledCheckbox("Show Full Render", true);

    section("Datastructure");
    disabledCheckbox("Build KD-Tree", false);
    if (gi) {
        if (ImGui::Checkbox("Build Uniform Grid", &gi->photon_mapping)) world_changed = true;
    } else {
        disabledCheckbox("Build Uniform Grid", false);
    }
    disabledInputInt("Grid Size", 0);
    disabledInputInt("Neighbor", 1);

    if (gi) {
        if (ImGui::DragFloat(
                "Search Radius GI",
                &gi->photon_radius,
                0.001f,
                0.0f,
                1000.0f,
                "%.3f"))
        {
            gi->photon_radius = std::max(gi->photon_radius, 0.0f);
            world_changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = automatic radius");
    } else {
        disabledSliderFloat("Search Radius GI", 0.0f, 0.0f, 1.0f);
    }
    disabledSliderFloat("Search Radius Ca", 0.0f, 0.0f, 1.0f);
    dataStructureCombo();
    disabledCheckbox("Use Cone Filter", false);
    disabledSliderFloat("Filter Constant", 1.0f, 0.0f, 2.0f);

    section("Tone Mapping");
    const bool trace_renderer = renderer != RendererChoice::Rasterizer;
    toneMapperCombo(trace_renderer);
    if (renderer == RendererChoice::RayTracer) {
        ImGui::DragFloat("Exposure", &ray_tracer.settings().exposure, 0.01f, 0.0f, 32.0f, "%.3f");
    } else if (renderer == RendererChoice::PathTracer) {
        ImGui::DragFloat("Exposure", &path_tracer.settings().exposure, 0.01f, 0.0f, 32.0f, "%.3f");
    } else {
        disabledSliderFloat("Exposure", 1.0f, 0.0f, 4.0f);
    }
    disabledCheckbox("Activate Gamma Correction?", true);
    disabledSliderFloat("Gamma", 2.2f, 1.0f, 5.0f);

    if (light) {
        const float speed = std::max(std::abs(light->intensity) * 0.005f, 0.01f);
        if (ImGui::DragFloat("Adjust PointBrightness", &light->intensity, speed, 0.0f, 0.0f, "%.3f")) {
            light->intensity = std::max(light->intensity, 0.0f);
            world_changed = true;
        }
    } else {
        disabledSliderFloat("Adjust PointBrightness", 0.0f, 0.0f, 1.0f);
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

    ImGui::SetNextWindowPos(ImVec2(450.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330.0f, 332.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Full Rendering")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(190.0f);
    int samples = path_tracer.settings().samples_per_frame;
    if (ImGui::SliderInt("Samples / Frame", &samples, 1, 4))
        path_tracer.settings().samples_per_frame = samples;

    section("Uniform Grid");
    disabledInputInt("Neighbor", 1);

    section("Photon Gathering");
    if (gi) {
        if (ImGui::DragFloat(
                "Search Radius GI",
                &gi->photon_radius,
                0.001f,
                0.0f,
                1000.0f,
                "%.3f"))
        {
            gi->photon_radius = std::max(gi->photon_radius, 0.0f);
            world_changed = true;
        }
    } else {
        disabledSliderFloat("Search Radius GI", 0.0f, 0.0f, 1.0f);
    }
    disabledSliderFloat("Search Radius Ca", 0.0f, 0.0f, 1.0f);

    section("Which Datastructure");
    dataStructureCombo();

    ImGui::BeginDisabled();
    ImGui::Button("RENDER");
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::BeginDisabled();
    ImGui::Button("Save to File");
    ImGui::EndDisabled();
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
    ImGui::SetNextWindowPos(ImVec2(450.0f, 350.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250.0f, 142.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scene Manager")) {
        ImGui::End();
        return;
    }

    ImGui::PushItemWidth(145.0f);
    if (ImGui::BeginCombo("Scenes", name)) {
        ImGui::Selectable(name, true);
        ImGui::SetItemDefaultFocus();
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled();
    ImGui::Button("Load Scene");
    ImGui::EndDisabled();
    ImGui::Separator();
    rendererSelector(renderer, available);
    ImGui::PopItemWidth();
    ImGui::End();
}

void informationWindow()
{
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(ImVec2(450.0f, 500.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330.0f, 190.0f), ImGuiCond_FirstUseEver);
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
    if (statistics.calculating) {
        ImGui::ProgressBar(statistics.progress, ImVec2(-1.0f, 0.0f), "GI building");
    }

    ImGui::End();
}

void debugWindow()
{
    const Renderer::GlobalIllumination::Debug::Statistics statistics =
        Renderer::GlobalIllumination::Debug::statistics();

    ImGui::SetNextWindowPos(ImVec2(790.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(315.0f, 315.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug View")) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Wireframe", &debug_settings.wireframe);
    ImGui::Checkbox("Show BVH", &debug_settings.show_bvh);
    ImGui::Checkbox("Show Photon Map", &debug_settings.show_photons);
    ImGui::Checkbox("Show GI Probe Grid", &debug_settings.show_gi_probes);
    ImGui::Checkbox("Show Scene Bounds", &debug_settings.show_scene_bounds);
    ImGui::Checkbox("Show Light", &debug_settings.show_light);
    ImGui::Checkbox("Show Triangle Normals", &debug_settings.show_normals);

    const int maximum_level = std::max(static_cast<int>(statistics.bvh_depth) - 1, 0);
    debug_settings.bvh_level = std::clamp(debug_settings.bvh_level, 0, maximum_level);
    ImGui::BeginDisabled(maximum_level == 0);
    ImGui::SliderInt("BVH Level", &debug_settings.bvh_level, 0, maximum_level);
    ImGui::EndDisabled();
    ImGui::SliderInt("Wireframe Triangles", &debug_settings.wireframe_triangles, 1000, 50000);
    ImGui::SliderFloat("Overlay Opacity", &debug_settings.overlay_opacity, 0.15f, 1.0f, "%.2f");
    ImGui::SliderFloat("Photon Point Size", &debug_settings.photon_size, 1.0f, 5.0f, "%.1f");

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
    if (!ImGui::GetCurrentContext()) return;

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

    if (world_changed) world.markChanged();
    if (reset_requested) {
        Renderer::GlobalIllumination::reset();
        world.markChanged();
    }

    drawOverlays(world);
}

} // namespace UI
