#include "Renderer/Debug/Debug.hpp"

#include "Camera.hpp"
#include "Renderer/Debug/Internal.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Renderer::Debug {
namespace {

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

double volume(Vec3 minimum, Vec3 maximum)
{
    const double x = std::max(static_cast<double>(maximum.x - minimum.x), 0.0);
    const double y = std::max(static_cast<double>(maximum.y - minimum.y), 0.0);
    const double z = std::max(static_cast<double>(maximum.z - minimum.z), 0.0);
    return x * y * z;
}

} // namespace

struct Inspector::Impl {
    bool show_bvh = false;
    bool show_viewport = false;
    int bvh_level = 0;
    float overlay_opacity = 0.0f;
    Vec4 bvh_color{};
    Vec4 highlight_color{};
    Vec4 player_color{};
    float player_height = 0.0f;
    float camera_marker_size = 0.0f;

    bool frozen = false;
    Ecs::Entity player_camera = Ecs::INVALID_ENTITY;
    Ecs::Entity debug_camera = Ecs::INVALID_ENTITY;
    Transform frozen_player_transform{};
    Camera::CameraComponent frozen_player_camera{};
    Visibility::Result frozen_visibility{};
    Scenes::SceneCache frozen_cache;
    std::vector<std::pair<Ecs::Entity, bool>> camera_activity;

    const Ecs::World *live_world = nullptr;
    Scenes::Scene::RenderRevision live_revision{};
    Scenes::SceneCache live_cache;
    const Scenes::SceneCache *bvh_cache = nullptr;
    std::vector<std::vector<std::uint32_t>> bvh_levels;
    BvhInfo bvh_info{};
    std::vector<Internal::Vertex> lines;

    void invalidateBvhMetadata()
    {
        bvh_cache = nullptr;
        bvh_levels.clear();
        bvh_info = {};
    }

    void prepareBvhMetadata(const Scenes::SceneCache& cache)
    {
        if (bvh_cache == &cache) return;

        bvh_cache = &cache;
        bvh_levels.clear();

        const auto& nodes = cache.nodes();
        if (nodes.empty()) return;

        std::vector<std::uint32_t> current{0u};
        std::vector<std::uint32_t> next;
        while (!current.empty()) {
            bvh_levels.push_back(current);
            next.clear();
            next.reserve(current.size() * 2u);

            for (const std::uint32_t index : current) {
                if (index >= nodes.size()) continue;
                const Scenes::GpuNode& node = nodes[index];
                if ((node.meta & Scenes::LeafBit) != 0u) continue;
                if (node.first < nodes.size()) next.push_back(node.first);
                if (node.meta < nodes.size()) next.push_back(node.meta);
            }

            current.swap(next);
        }
    }

    Internal::Vertex vertex(Vec3 position, Vec4 color) const
    {
        return {
            {position.x, position.y, position.z, 1.0f},
            {
                color.x,
                color.y,
                color.z,
                color.w * std::clamp(overlay_opacity, 0.0f, 1.0f),
            },
        };
    }

    void addLine(Vec3 a, Vec3 b, Vec4 color)
    {
        lines.push_back(vertex(a, color));
        lines.push_back(vertex(b, color));
    }

    void addAabb(Vec3 minimum, Vec3 maximum, Vec4 color)
    {
        const Vec3 p000 {minimum.x, minimum.y, minimum.z};
        const Vec3 p100 {maximum.x, minimum.y, minimum.z};
        const Vec3 p110 {maximum.x, maximum.y, minimum.z};
        const Vec3 p010 {minimum.x, maximum.y, minimum.z};
        const Vec3 p001 {minimum.x, minimum.y, maximum.z};
        const Vec3 p101 {maximum.x, minimum.y, maximum.z};
        const Vec3 p111 {maximum.x, maximum.y, maximum.z};
        const Vec3 p011 {minimum.x, maximum.y, maximum.z};

        addLine(p000, p100, color);
        addLine(p100, p110, color);
        addLine(p110, p010, color);
        addLine(p010, p000, color);
        addLine(p001, p101, color);
        addLine(p101, p111, color);
        addLine(p111, p011, color);
        addLine(p011, p001, color);
        addLine(p000, p001, color);
        addLine(p100, p101, color);
        addLine(p110, p111, color);
        addLine(p010, p011, color);
    }

    void addViewport(const Visibility::Frustum& frustum)
    {
        if (!frustum.valid) return;
        const auto corners = Visibility::system().corners(frustum);
        for (std::size_t base : {std::size_t{0u}, std::size_t{4u}}) {
            addLine(corners[base + 0u], corners[base + 1u], bvh_color);
            addLine(corners[base + 1u], corners[base + 2u], bvh_color);
            addLine(corners[base + 2u], corners[base + 3u], bvh_color);
            addLine(corners[base + 3u], corners[base + 0u], bvh_color);
        }
        for (std::size_t index = 0u; index < 4u; ++index)
            addLine(corners[index], corners[index + 4u], bvh_color);
    }

    void addPlayerMarker(const Visibility::Frustum& frustum)
    {
        if (!frozen || !frustum.valid) return;
        const Vec3 head = frozen_player_transform.position;
        const Vec3 feet = subtract(head, multiply(frustum.up, player_height));
        addLine(head, feet, player_color);

        const Vec3 right = multiply(frustum.right, camera_marker_size);
        const Vec3 up = multiply(frustum.up, camera_marker_size);
        const Vec3 a = subtract(subtract(head, right), up);
        const Vec3 b = add(subtract(head, up), right);
        const Vec3 c = add(add(head, right), up);
        const Vec3 d = add(subtract(head, right), up);
        addLine(a, b, player_color);
        addLine(b, c, player_color);
        addLine(c, d, player_color);
        addLine(d, a, player_color);
    }

    const Scenes::SceneCache *cacheFor(const Ecs::World& world)
    {
        if (frozen) return &frozen_cache;
        const Scenes::Scene::RenderRevision revision = Scenes::Scene::renderRevision(world);
        if (live_world == &world && live_revision == revision) return &live_cache;

        std::vector<Scenes::Scene::RenderItem> items;
        Scenes::Scene::collectRenderItems(world, items);
        std::string error;
        if (!live_cache.sync(
                world,
                items,
                std::numeric_limits<std::size_t>::max(),
                &error))
        {
            live_cache.clear();
            invalidateBvhMetadata();
            return nullptr;
        }
        invalidateBvhMetadata();
        live_world = &world;
        live_revision = revision;
        return &live_cache;
    }

    Visibility::Frustum sourceFrustum(const Ecs::World& world, int width, int height) const
    {
        return frozen
            ? frozen_visibility.frustum
            : Visibility::system().makeFrustum(world, width, height);
    }

    void addBvh(
        const Scenes::SceneCache& cache,
        Vec3 camera_position)
    {
        const auto& nodes = cache.nodes();
        if (nodes.empty()) return;

        prepareBvhMetadata(cache);
        if (bvh_levels.empty()) return;

        const int maximum_level = static_cast<int>(bvh_levels.size()) - 1;
        const int level_index = std::clamp(bvh_level, 0, maximum_level);
        const std::vector<std::uint32_t>& level = bvh_levels[static_cast<std::size_t>(level_index)];

        std::uint32_t selected = std::numeric_limits<std::uint32_t>::max();
        double selected_volume = std::numeric_limits<double>::infinity();
        std::size_t containing_nodes = 0u;
        for (const std::uint32_t index : level) {
            const Vec3 minimum = nodeMinimum(nodes[index]);
            const Vec3 maximum = nodeMaximum(nodes[index]);
            if (!inside(camera_position, minimum, maximum)) continue;

            ++containing_nodes;
            const double candidate_volume = volume(minimum, maximum);
            if (candidate_volume < selected_volume ||
                (candidate_volume == selected_volume && index < selected))
            {
                selected = index;
                selected_volume = candidate_volume;
            }
        }

        bvh_info.available = true;
        bvh_info.selected = selected != std::numeric_limits<std::uint32_t>::max();
        bvh_info.level = level_index;
        bvh_info.maximum_level = maximum_level;
        bvh_info.total_nodes = nodes.size();
        bvh_info.level_nodes = level.size();
        bvh_info.containing_nodes = containing_nodes;
        bvh_info.selected_node = bvh_info.selected ? static_cast<std::size_t>(selected) : 0u;

        for (const std::uint32_t index : level) {
            addAabb(
                nodeMinimum(nodes[index]),
                nodeMaximum(nodes[index]),
                index == selected ? highlight_color : bvh_color
            );
        }
    }
};

Inspector::Inspector() : impl_(new Impl) {}

Inspector::~Inspector()
{
    delete impl_;
    impl_ = nullptr;
}

void Inspector::setShowBvh(bool value) { impl_->show_bvh = value; }
void Inspector::setShowViewport(bool value) { impl_->show_viewport = value; }
void Inspector::setBvhLevel(int value) { impl_->bvh_level = value; }
void Inspector::setOverlayOpacity(float value) { impl_->overlay_opacity = value; }
void Inspector::setBvhColor(Vec4 value) { impl_->bvh_color = value; }
void Inspector::setHighlightColor(Vec4 value) { impl_->highlight_color = value; }
void Inspector::setPlayerColor(Vec4 value) { impl_->player_color = value; }
void Inspector::setPlayerHeight(float value) { impl_->player_height = value; }
void Inspector::setCameraMarkerSize(float value) { impl_->camera_marker_size = value; }

bool Inspector::showBvh() const { return impl_->show_bvh; }
bool Inspector::showViewport() const { return impl_->show_viewport; }
int Inspector::bvhLevel() const { return impl_->bvh_level; }
float Inspector::overlayOpacity() const { return impl_->overlay_opacity; }

bool Inspector::freeze(Ecs::World& world, int width, int height)
{
    if (impl_->frozen) return true;

    const Scenes::Scene::CameraState player = Scenes::Scene::cameraState(world);
    if (!player.valid) return false;
    const Camera::CameraComponent *player_camera = world.get<Camera::CameraComponent>(player.entity);
    if (!player_camera) return false;

    Visibility::Result visibility = Visibility::system().evaluate(world, width, height);
    if (!visibility.frustum.valid) return false;

    std::vector<Scenes::Scene::RenderItem> items;
    Scenes::Scene::collectRenderItems(world, items);
    std::string error;
    Scenes::SceneCache snapshot_cache;
    if (!snapshot_cache.sync(
            world,
            items,
            std::numeric_limits<std::size_t>::max(),
            &error))
    {
        return false;
    }

    impl_->player_camera = player.entity;
    impl_->frozen_player_transform = player.transform;
    impl_->frozen_player_camera = *player_camera;
    impl_->frozen_visibility = std::move(visibility);
    impl_->frozen_cache = std::move(snapshot_cache);
    impl_->invalidateBvhMetadata();

    impl_->camera_activity.clear();
    for (const Ecs::Entity entity : world.entities()) {
        if (Camera::CameraComponent *camera = world.get<Camera::CameraComponent>(entity))
            impl_->camera_activity.emplace_back(entity, camera->active);
    }

    for (const auto& [entity, active] : impl_->camera_activity) {
        (void)active;
        if (Camera::CameraComponent *camera = world.get<Camera::CameraComponent>(entity))
            camera->active = false;
    }

    impl_->debug_camera = world.createEntity();
    world.add<Transform>(impl_->debug_camera, impl_->frozen_player_transform);
    Camera::CameraComponent debug_camera = impl_->frozen_player_camera;
    debug_camera.active = true;
    world.add<Camera::CameraComponent>(impl_->debug_camera, debug_camera);
    impl_->frozen = true;
    Visibility::system().setOverride(impl_->frozen_visibility);
    world.markChanged();
    return true;
}

void Inspector::unfreeze(Ecs::World& world)
{
    if (!impl_->frozen) return;

    Visibility::system().clearOverride();

    if (impl_->debug_camera != Ecs::INVALID_ENTITY && world.alive(impl_->debug_camera))
        world.destroyEntity(impl_->debug_camera);

    for (const auto& [entity, active] : impl_->camera_activity) {
        if (!world.alive(entity)) continue;
        if (Camera::CameraComponent *camera = world.get<Camera::CameraComponent>(entity))
            camera->active = active;
    }

    impl_->frozen = false;
    impl_->player_camera = Ecs::INVALID_ENTITY;
    impl_->debug_camera = Ecs::INVALID_ENTITY;
    impl_->frozen_visibility = {};
    impl_->frozen_cache.clear();
    impl_->camera_activity.clear();
    impl_->live_world = nullptr;
    impl_->live_revision = {};
    impl_->live_cache.clear();
    impl_->invalidateBvhMetadata();
    world.markChanged();
}

bool Inspector::frozen() const { return impl_->frozen; }
Ecs::Entity Inspector::debugCamera() const { return impl_->debug_camera; }

SnapshotInfo Inspector::snapshotInfo() const
{
    SnapshotInfo result;
    result.frozen = impl_->frozen;
    result.visible_entities = impl_->frozen_visibility.visible.size();
    result.culled_entities = impl_->frozen_visibility.culled.size();
    result.visible_triangles = impl_->frozen_visibility.visible_triangles;
    result.culled_triangles = impl_->frozen_visibility.culled_triangles;
    result.player_camera = impl_->player_camera;
    result.debug_camera = impl_->debug_camera;
    return result;
}

BvhInfo Inspector::bvhInfo() const
{
    return impl_->bvh_info;
}

void Inspector::clear(Ecs::World *world)
{
    if (impl_->frozen && world) unfreeze(*world);
    if (impl_->frozen) return;
    Visibility::system().clearOverride();
    impl_->live_world = nullptr;
    impl_->live_revision = {};
    impl_->live_cache.clear();
    impl_->invalidateBvhMetadata();
    impl_->lines.clear();
}

Inspector& inspector()
{
    static Inspector value;
    return value;
}

void clear()
{
    inspector().clear();
}

void shutdown()
{
    Internal::shutdownSDLGPU();
    inspector().clear();
}

namespace Internal {

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output)
{
    Inspector& public_inspector = inspector();
    Inspector::Impl& state = *public_inspector.impl_;
    if (!state.show_bvh && !state.show_viewport) return;

    const Scenes::Scene::CameraState camera = Scenes::Scene::cameraState(world);
    if (!camera.valid) return;

    const Scenes::SceneCache *cache = state.cacheFor(world);
    if (!cache) return;

    const Vec3 camera_position = camera.transform.position;
    const Vec3 camera_forward = Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    const Vec3 camera_right = Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    const Vec3 camera_up = Math::normalize(Math::cross(camera_right, camera_forward));

    state.lines.clear();
    if (state.show_bvh) state.addBvh(*cache, camera_position);

    const Visibility::Frustum source_frustum = state.sourceFrustum(world, output.width, output.height);
    if (state.show_viewport) {
        state.addViewport(source_frustum);
        state.addPlayerMarker(source_frustum);
    }

    if (state.lines.empty()) return;

    const float aspect = static_cast<float>(std::max(output.width, 1)) /
        static_cast<float>(std::max(output.height, 1));
    const float far_distance = Visibility::system().farDistance();
    if (far_distance <= camera.near_plane) return;

    const Math::Mat4 projection = Math::perspective(
        camera.fov_degrees,
        aspect,
        camera.near_plane,
        far_distance
    );
    const Math::Mat4 view = Math::viewMatrix(
        camera_position,
        camera_forward,
        camera_right,
        camera_up
    );

    renderSDLGPU(state.lines, projection, view, output);
}

} // namespace Internal
} // namespace Renderer::Debug
