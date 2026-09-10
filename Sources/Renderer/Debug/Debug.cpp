#include "Renderer/Debug/Debug.hpp"

#include "Camera.hpp"
#include "Renderer/Debug/Internal.hpp"
#include "Renderer/GlobalIllumination/Debug.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/GlobalIllumination/TraceScene.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::Debug {
namespace {

Settings debug_settings;

struct State {
    const Ecs::World *wireframe_world = nullptr;
    std::uint64_t wireframe_signature = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t wireframe_revision = 1u;
    std::vector<Internal::Vertex> wireframe;
    std::vector<Internal::Vertex> dynamic;
};

State state;

constexpr Vec4 kBlue {0.20f, 0.52f, 1.00f, 1.00f};
constexpr Vec4 kYellow {1.00f, 0.82f, 0.16f, 1.00f};
constexpr float kRayEpsilon = 1.0e-5f;

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float lengthSquared(Vec3 value)
{
    return Math::dot(value, value);
}

float length(Vec3 value)
{
    return std::sqrt(std::max(lengthSquared(value), 0.0f));
}

Internal::Vertex vertex(Vec3 position, Vec4 color)
{
    return {
        {position.x, position.y, position.z, 1.0f},
        {color.x, color.y, color.z, color.w * std::clamp(debug_settings.overlay_opacity, 0.0f, 1.0f)},
    };
}

void addLine(std::vector<Internal::Vertex>& out, Vec3 a, Vec3 b, Vec4 color)
{
    out.push_back(vertex(a, color));
    out.push_back(vertex(b, color));
}

Vec3 trianglePosition(const std::array<float, 4>& value)
{
    return {value[0], value[1], value[2]};
}

void addTriangle(
    std::vector<Internal::Vertex>& out,
    const Scenes::GpuTriangle& triangle,
    Vec4 color)
{
    const Vec3 a = trianglePosition(triangle.p0);
    const Vec3 b = trianglePosition(triangle.p1);
    const Vec3 c = trianglePosition(triangle.p2);
    addLine(out, a, b, color);
    addLine(out, b, c, color);
    addLine(out, c, a, color);
}

Vec3 nodeMinimum(const Scenes::GpuNode& node)
{
    return {node.min_x, node.min_y, node.min_z};
}

Vec3 nodeMaximum(const Scenes::GpuNode& node)
{
    return {node.max_x, node.max_y, node.max_z};
}

bool inside(Vec3 point, Vec3 minimum, Vec3 maximum)
{
    return point.x >= minimum.x && point.x <= maximum.x &&
        point.y >= minimum.y && point.y <= maximum.y &&
        point.z >= minimum.z && point.z <= maximum.z;
}

bool rayAabb(Vec3 origin, Vec3 direction, Vec3 minimum, Vec3 maximum, float& distance)
{
    float near_t = 0.0f;
    float far_t = std::numeric_limits<float>::infinity();

    const auto slab = [&](float origin_axis, float direction_axis, float minimum_axis, float maximum_axis) {
        if (std::abs(direction_axis) <= kRayEpsilon)
            return origin_axis >= minimum_axis && origin_axis <= maximum_axis;

        float a = (minimum_axis - origin_axis) / direction_axis;
        float b = (maximum_axis - origin_axis) / direction_axis;
        if (a > b) std::swap(a, b);
        near_t = std::max(near_t, a);
        far_t = std::min(far_t, b);
        return near_t <= far_t;
    };

    if (!slab(origin.x, direction.x, minimum.x, maximum.x) ||
        !slab(origin.y, direction.y, minimum.y, maximum.y) ||
        !slab(origin.z, direction.z, minimum.z, maximum.z))
    {
        return false;
    }

    distance = near_t;
    return far_t >= 0.0f;
}

void addAabb(
    std::vector<Internal::Vertex>& out,
    Vec3 minimum,
    Vec3 maximum,
    Vec4 color)
{
    const Vec3 p000 {minimum.x, minimum.y, minimum.z};
    const Vec3 p100 {maximum.x, minimum.y, minimum.z};
    const Vec3 p110 {maximum.x, maximum.y, minimum.z};
    const Vec3 p010 {minimum.x, maximum.y, minimum.z};
    const Vec3 p001 {minimum.x, minimum.y, maximum.z};
    const Vec3 p101 {maximum.x, minimum.y, maximum.z};
    const Vec3 p111 {maximum.x, maximum.y, maximum.z};
    const Vec3 p011 {minimum.x, maximum.y, maximum.z};

    addLine(out, p000, p100, color);
    addLine(out, p100, p110, color);
    addLine(out, p110, p010, color);
    addLine(out, p010, p000, color);
    addLine(out, p001, p101, color);
    addLine(out, p101, p111, color);
    addLine(out, p111, p011, color);
    addLine(out, p011, p001, color);
    addLine(out, p000, p001, color);
    addLine(out, p100, p101, color);
    addLine(out, p110, p111, color);
    addLine(out, p010, p011, color);
}

void collectBvhLevel(
    const std::vector<Scenes::GpuNode>& nodes,
    std::uint32_t index,
    int depth,
    int target,
    std::vector<std::uint32_t>& out)
{
    if (index >= nodes.size()) return;
    const Scenes::GpuNode& node = nodes[index];
    const bool leaf = (node.meta & Scenes::LeafBit) != 0u;
    if (depth == target || leaf) {
        if (depth == target) out.push_back(index);
        return;
    }
    if (depth > target) return;
    collectBvhLevel(nodes, node.first, depth + 1, target, out);
    collectBvhLevel(nodes, node.meta, depth + 1, target, out);
}

Vec3 probePosition(
    const GlobalIllumination::Field& field,
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

void addCross(std::vector<Internal::Vertex>& out, Vec3 point, float size, Vec4 color)
{
    addLine(out, {point.x - size, point.y, point.z}, {point.x + size, point.y, point.z}, color);
    addLine(out, {point.x, point.y - size, point.z}, {point.x, point.y + size, point.z}, color);
    addLine(out, {point.x, point.y, point.z - size}, {point.x, point.y, point.z + size}, color);
}

float distanceToRay(Vec3 point, Vec3 origin, Vec3 direction)
{
    const Vec3 to_point = subtract(point, origin);
    const float along = std::max(Math::dot(to_point, direction), 0.0f);
    return length(subtract(to_point, multiply(direction, along)));
}

bool anyEnabled()
{
    return debug_settings.wireframe ||
        debug_settings.show_bvh ||
        debug_settings.show_photons ||
        debug_settings.show_gi_probes ||
        debug_settings.show_scene_bounds ||
        debug_settings.show_light ||
        debug_settings.show_normals;
}

void syncWireframe(
    const Ecs::World& world,
    const GlobalIllumination::TraceScene& trace_scene)
{
    if (!debug_settings.wireframe) return;
    const Scenes::SceneCache& cache = trace_scene.cache();
    if (cache.triangles().empty()) return;

    const std::uint64_t signature = trace_scene.signature(world, cache.renderItems());
    if (state.wireframe_world == &world && state.wireframe_signature == signature) return;

    state.wireframe.clear();
    state.wireframe.reserve(cache.triangles().size() * 6u);
    for (const Scenes::GpuTriangle& triangle : cache.triangles())
        addTriangle(state.wireframe, triangle, kBlue);

    state.wireframe_world = &world;
    state.wireframe_signature = signature;
    ++state.wireframe_revision;
}

void appendWireframeHighlight(
    const GlobalIllumination::TraceScene& trace_scene,
    Vec3 camera_position,
    Vec3 camera_forward)
{
    if (!debug_settings.wireframe) return;
    const GlobalIllumination::TraceHit hit = trace_scene.traceClosest(
        camera_position,
        camera_forward,
        std::numeric_limits<float>::infinity()
    );
    const auto& triangles = trace_scene.cache().triangles();
    if (hit.found && hit.triangle < triangles.size())
        addTriangle(state.dynamic, triangles[hit.triangle], kYellow);
}

void appendBvh(
    const GlobalIllumination::TraceScene& trace_scene,
    Vec3 camera_position,
    Vec3 camera_forward)
{
    if (!debug_settings.show_bvh) return;
    const auto& nodes = trace_scene.cache().nodes();
    if (nodes.empty()) return;

    const GlobalIllumination::Debug::Statistics statistics =
        GlobalIllumination::Debug::statistics();
    const int maximum_level = std::max(static_cast<int>(statistics.bvh_depth) - 1, 0);
    debug_settings.bvh_level = std::clamp(debug_settings.bvh_level, 0, maximum_level);

    std::vector<std::uint32_t> level;
    collectBvhLevel(nodes, 0u, 0, debug_settings.bvh_level, level);

    std::uint32_t selected = std::numeric_limits<std::uint32_t>::max();
    for (const std::uint32_t index : level) {
        if (inside(camera_position, nodeMinimum(nodes[index]), nodeMaximum(nodes[index]))) {
            selected = index;
            break;
        }
    }

    if (selected == std::numeric_limits<std::uint32_t>::max()) {
        float best_distance = std::numeric_limits<float>::infinity();
        for (const std::uint32_t index : level) {
            float distance = 0.0f;
            if (!rayAabb(
                    camera_position,
                    camera_forward,
                    nodeMinimum(nodes[index]),
                    nodeMaximum(nodes[index]),
                    distance))
            {
                continue;
            }
            if (distance < best_distance) {
                best_distance = distance;
                selected = index;
            }
        }
    }

    for (const std::uint32_t index : level) {
        addAabb(
            state.dynamic,
            nodeMinimum(nodes[index]),
            nodeMaximum(nodes[index]),
            index == selected ? kYellow : kBlue
        );
    }
}

void appendPhotons(Vec3 camera_position, Vec3 camera_forward, float scene_scale)
{
    if (!debug_settings.show_photons) return;
    const auto& map = GlobalIllumination::Debug::photonMap();
    const auto *photons = map.photonData();
    const std::size_t count = map.photonCount();
    if (!photons || count == 0u) return;

    constexpr std::size_t maximum_markers = 10000u;
    const std::size_t stride = std::max<std::size_t>(1u, (count + maximum_markers - 1u) / maximum_markers);
    const float marker_size = scene_scale * 0.0015f * std::clamp(debug_settings.photon_size, 1.0f, 5.0f);

    std::size_t selected = 0u;
    float best = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0u; index < count; ++index) {
        const float score = distanceToRay(photons[index].position, camera_position, camera_forward);
        if (score < best) {
            best = score;
            selected = index;
        }
    }

    for (std::size_t index = 0u; index < count; index += stride)
        addCross(state.dynamic, photons[index].position, marker_size, index == selected ? kYellow : kBlue);
    addCross(state.dynamic, photons[selected].position, marker_size * 1.8f, kYellow);
}

void appendProbes(Vec3 camera_position, float scene_scale)
{
    if (!debug_settings.show_gi_probes) return;
    const GlobalIllumination::Field *field = GlobalIllumination::Debug::field();
    if (!field || !field->valid()) return;

    Vec3 nearest{};
    float nearest_distance = std::numeric_limits<float>::infinity();
    const float marker_size = scene_scale * 0.003f;

    for (std::uint32_t z = 0u; z < field->size_z; ++z) {
        for (std::uint32_t y = 0u; y < field->size_y; ++y) {
            for (std::uint32_t x = 0u; x < field->size_x; ++x) {
                const Vec3 point = probePosition(*field, x, y, z);
                const float distance = lengthSquared(subtract(point, camera_position));
                if (distance < nearest_distance) {
                    nearest_distance = distance;
                    nearest = point;
                }
                addCross(state.dynamic, point, marker_size, kBlue);
            }
        }
    }
    addCross(state.dynamic, nearest, marker_size * 1.8f, kYellow);
}

void appendNormals(
    const GlobalIllumination::TraceScene& trace_scene,
    Vec3 camera_position,
    Vec3 camera_forward,
    float scene_scale)
{
    if (!debug_settings.show_normals) return;
    const auto& triangles = trace_scene.cache().triangles();
    if (triangles.empty()) return;

    constexpr std::size_t maximum_normals = 2500u;
    const std::size_t stride = std::max<std::size_t>(1u, (triangles.size() + maximum_normals - 1u) / maximum_normals);
    const float normal_length = scene_scale * 0.012f;

    for (std::size_t index = 0u; index < triangles.size(); index += stride) {
        const Scenes::GpuTriangle& triangle = triangles[index];
        const Vec3 a = trianglePosition(triangle.p0);
        const Vec3 b = trianglePosition(triangle.p1);
        const Vec3 c = trianglePosition(triangle.p2);
        const Vec3 center = multiply(add(add(a, b), c), 1.0f / 3.0f);
        const Vec3 normal = Math::normalize({
            triangle.n0[0] + triangle.n1[0] + triangle.n2[0],
            triangle.n0[1] + triangle.n1[1] + triangle.n2[1],
            triangle.n0[2] + triangle.n1[2] + triangle.n2[2],
        });
        addLine(state.dynamic, center, add(center, multiply(normal, normal_length)), kBlue);
    }

    const GlobalIllumination::TraceHit hit = trace_scene.traceClosest(
        camera_position,
        camera_forward,
        std::numeric_limits<float>::infinity()
    );
    if (hit.found)
        addLine(state.dynamic, hit.position, add(hit.position, multiply(hit.normal, normal_length * 2.0f)), kYellow);
}

} // namespace

Settings& settings()
{
    return debug_settings;
}

void clear()
{
    state.wireframe_world = nullptr;
    state.wireframe_signature = std::numeric_limits<std::uint64_t>::max();
    state.wireframe.clear();
    state.dynamic.clear();
    ++state.wireframe_revision;
}

void shutdown()
{
    Internal::shutdownOpenGL();
#ifdef __APPLE__
    Internal::shutdownMetal();
#endif
    clear();
}

namespace Internal {

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output)
{
    if (!anyEnabled()) return;

    const GlobalIllumination::TraceScene& trace_scene = GlobalIllumination::Debug::traceScene();
    if (trace_scene.empty()) return;

    const Scenes::Scene::CameraState camera = Scenes::Scene::cameraState(world);
    if (!camera.valid) return;

    const Vec3 camera_position = camera.transform.position;
    const Vec3 camera_forward = Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    const Vec3 camera_right = Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    const Vec3 camera_up = Math::normalize(Math::cross(camera_right, camera_forward));

    const GlobalIllumination::TraceBounds bounds = trace_scene.bounds();
    float scene_scale = 1.0f;
    if (bounds.valid) {
        const Vec3 extent = subtract(bounds.maximum, bounds.minimum);
        scene_scale = std::max({extent.x, extent.y, extent.z, 1.0f});
    }

    syncWireframe(world, trace_scene);
    state.dynamic.clear();
    state.dynamic.reserve(32768u);

    appendWireframeHighlight(trace_scene, camera_position, camera_forward);
    appendBvh(trace_scene, camera_position, camera_forward);
    appendPhotons(camera_position, camera_forward, scene_scale);
    appendProbes(camera_position, scene_scale);
    appendNormals(trace_scene, camera_position, camera_forward, scene_scale);

    if (debug_settings.show_scene_bounds && bounds.valid) {
        addAabb(
            state.dynamic,
            bounds.minimum,
            bounds.maximum,
            inside(camera_position, bounds.minimum, bounds.maximum) ? kYellow : kBlue
        );
    }

    if (debug_settings.show_light) {
        const Scenes::LightState light = Scenes::lightState(Scenes::Scene::lightState(world));
        if (light.valid) {
            bool relevant = false;
            if (light.type == LightType::Directional) {
                relevant = Math::dot(camera_forward, multiply(Math::normalize(light.direction), -1.0f)) > 0.94f;
            } else {
                relevant = distanceToRay(light.position, camera_position, camera_forward) < scene_scale * 0.04f ||
                    length(subtract(light.position, camera_position)) < scene_scale * 0.08f;
            }

            const float marker = scene_scale * 0.012f;
            addCross(state.dynamic, light.position, marker, relevant ? kYellow : kBlue);
            if (light.type == LightType::Directional) {
                addLine(
                    state.dynamic,
                    light.position,
                    add(light.position, multiply(Math::normalize(light.direction), scene_scale * 0.15f)),
                    relevant ? kYellow : kBlue
                );
            }
        }
    }

    if ((!debug_settings.wireframe || state.wireframe.empty()) && state.dynamic.empty()) return;

    const float aspect = static_cast<float>(std::max(output.width, 1)) /
        static_cast<float>(std::max(output.height, 1));
    const float near_plane = std::max(camera.near_plane, 1.0e-3f);
    const float far_plane = std::max(scene_scale * 12.0f, near_plane + 100.0f);
    const Math::Mat4 projection = Math::perspective(camera.fov_degrees, aspect, near_plane, far_plane);
    const Math::Mat4 view = Math::viewMatrix(camera_position, camera_forward, camera_right, camera_up);

    const std::vector<Vertex> empty;
    const std::vector<Vertex>& wireframe = debug_settings.wireframe ? state.wireframe : empty;

    switch (output.api) {
        case Renderer::Internal::GraphicsApi::OpenGL:
            renderOpenGL(wireframe, state.wireframe_revision, state.dynamic, projection, view, output);
            break;
        case Renderer::Internal::GraphicsApi::Metal:
#ifdef __APPLE__
            renderMetal(wireframe, state.wireframe_revision, state.dynamic, projection, view, output);
#endif
            break;
    }
}

} // namespace Internal
} // namespace Renderer::Debug
