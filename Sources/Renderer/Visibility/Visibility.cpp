#include "Renderer/Visibility/Visibility.hpp"

#include "Camera.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace Renderer::Visibility {
namespace {

constexpr float pi = 3.14159265358979323846f;

struct Plane {
    Vec3 normal{};
    float distance = 0.0f;
};

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

Plane plane(Vec3 normal, Vec3 point)
{
    normal = Math::normalize(normal);
    return {normal, -Math::dot(normal, point)};
}

float signedDistance(const Plane& value, Vec3 point)
{
    return Math::dot(value.normal, point) + value.distance;
}

std::array<Vec3, 8> itemCorners(const Scenes::Scene::RenderItem& item)
{
    std::array<Vec3, 8> result{};
    if (!item.mesh || !item.transform) return result;

    const Vec3 minimum {
        item.mesh->bounds.minimum.x,
        item.mesh->bounds.minimum.y,
        item.mesh->bounds.minimum.z,
    };
    const Vec3 maximum {
        item.mesh->bounds.maximum.x,
        item.mesh->bounds.maximum.y,
        item.mesh->bounds.maximum.z,
    };
    const Math::Mat4 model = Math::modelMatrix(*item.transform);

    const std::array<Vec3, 8> local {{
        {minimum.x, minimum.y, minimum.z},
        {maximum.x, minimum.y, minimum.z},
        {maximum.x, maximum.y, minimum.z},
        {minimum.x, maximum.y, minimum.z},
        {minimum.x, minimum.y, maximum.z},
        {maximum.x, minimum.y, maximum.z},
        {maximum.x, maximum.y, maximum.z},
        {minimum.x, maximum.y, maximum.z},
    }};

    for (std::size_t index = 0u; index < local.size(); ++index)
        result[index] = Math::transformPoint(model, local[index]);
    return result;
}

std::array<Plane, 6> planes(const Frustum& frustum)
{
    const Vec3 near_center = add(frustum.position, multiply(frustum.forward, frustum.near_distance));
    const Vec3 far_center = add(frustum.position, multiply(frustum.forward, frustum.far_distance));

    if (frustum.orthographic) {
        const Vec3 left = subtract(frustum.position, multiply(frustum.right, frustum.half_width));
        const Vec3 right = add(frustum.position, multiply(frustum.right, frustum.half_width));
        const Vec3 bottom = subtract(frustum.position, multiply(frustum.up, frustum.half_height));
        const Vec3 top = add(frustum.position, multiply(frustum.up, frustum.half_height));
        return {{
            plane(frustum.forward, near_center),
            plane(multiply(frustum.forward, -1.0f), far_center),
            plane(frustum.right, left),
            plane(multiply(frustum.right, -1.0f), right),
            plane(frustum.up, bottom),
            plane(multiply(frustum.up, -1.0f), top),
        }};
    }

    const float tangent_vertical = std::tan(frustum.half_vertical_fov_radians);
    const float tangent_horizontal = tangent_vertical * frustum.aspect;
    return {{
        plane(frustum.forward, near_center),
        plane(multiply(frustum.forward, -1.0f), far_center),
        plane(add(frustum.right, multiply(frustum.forward, tangent_horizontal)), frustum.position),
        plane(add(multiply(frustum.right, -1.0f), multiply(frustum.forward, tangent_horizontal)), frustum.position),
        plane(add(frustum.up, multiply(frustum.forward, tangent_vertical)), frustum.position),
        plane(add(multiply(frustum.up, -1.0f), multiply(frustum.forward, tangent_vertical)), frustum.position),
    }};
}

std::size_t triangleCount(const Scenes::Scene::RenderItem& item)
{
    return item.mesh ? item.mesh->indices.size() / 3u : 0u;
}

} // namespace

System& system()
{
    static System value;
    return value;
}

void System::setOverride(Result result)
{
    override_result_ = std::move(result);
    std::size_t size = 1u;
    for (const Ecs::Entity entity : override_result_.visible)
        size = std::max(size, static_cast<std::size_t>(entity) + 1u);

    override_visible_.assign(size, 0u);
    for (const Ecs::Entity entity : override_result_.visible) {
        if (static_cast<std::size_t>(entity) < override_visible_.size())
            override_visible_[entity] = 1u;
    }
    override_active_ = true;
}

void System::clearOverride()
{
    override_active_ = false;
    override_result_ = {};
    override_visible_.clear();
}

Frustum System::makeFrustum(const Ecs::World& world, int width, int height) const
{
    Frustum result;
    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    if (!camera.valid || width <= 0 || height <= 0) return result;

    const float safe_near = std::max(camera.near_plane, 1.0e-4f);
    float resolved_far = far_distance_;
    if (resolved_far <= safe_near && camera.far_plane > safe_near)
        resolved_far = camera.far_plane;
    else if (resolved_far > safe_near && camera.far_plane > safe_near)
        resolved_far = std::min(resolved_far, camera.far_plane);
    if (resolved_far <= safe_near) return result;

    result.position = camera.position;
    result.forward = camera.forward;
    result.right = camera.right;
    result.up = camera.up;
    result.near_distance = safe_near;
    result.far_distance = resolved_far;
    result.orthographic = camera.projection == Camera::Projection::Orthographic;

    if (result.orthographic) {
        result.half_width = std::max(std::abs(camera.xmag), 1.0e-6f);
        result.half_height = std::max(std::abs(camera.ymag), 1.0e-6f);
        result.aspect = result.half_width / result.half_height;
    } else {
        result.half_vertical_fov_radians =
            std::clamp(camera.fov_degrees, 1.0f, 179.0f) * (pi / 360.0f);
        result.aspect = camera.aspect_ratio > 1.0e-6f
            ? camera.aspect_ratio
            : static_cast<float>(width) / static_cast<float>(height);
    }

    result.valid = true;
    return result;
}

Classification System::classify(
    const Frustum& frustum,
    const Scenes::Scene::RenderItem& item) const
{
    if (!frustum.valid || !item.mesh || !item.transform) return Classification::Outside;

    const std::array<Vec3, 8> corners = itemCorners(item);
    bool intersecting = false;
    for (const Plane& clipping_plane : planes(frustum)) {
        float minimum = signedDistance(clipping_plane, corners[0]);
        float maximum = minimum;
        for (std::size_t index = 1u; index < corners.size(); ++index) {
            const float distance = signedDistance(clipping_plane, corners[index]);
            minimum = std::min(minimum, distance);
            maximum = std::max(maximum, distance);
        }
        if (maximum < 0.0f) return Classification::Outside;
        if (minimum < 0.0f) intersecting = true;
    }
    return intersecting ? Classification::Intersecting : Classification::Inside;
}

Result System::evaluate(const Ecs::World& world, int width, int height) const
{
    if (override_active_) return override_result_;
    std::vector<Scenes::Scene::RenderItem> visible_items;
    return collectVisibleRenderItems(world, width, height, visible_items);
}

Result System::collectVisibleRenderItems(
    const Ecs::World& world,
    int width,
    int height,
    std::vector<Scenes::Scene::RenderItem>& out) const
{
    Result result;
    std::vector<Scenes::Scene::RenderItem> items;
    Scenes::Scene::collectRenderItems(world, items);
    out.clear();

    if (override_active_) {
        result = override_result_;
        out.reserve(items.size());
        for (const Scenes::Scene::RenderItem& item : items) {
            const std::size_t entity = static_cast<std::size_t>(item.entity);
            if (entity < override_visible_.size() && override_visible_[entity] != 0u)
                out.push_back(item);
        }
        return result;
    }

    result.frustum = makeFrustum(world, width, height);

    if (!result.frustum.valid) {
        out = std::move(items);
        return result;
    }

    result.visible.reserve(items.size());
    result.culled.reserve(items.size());
    out.reserve(items.size());

    for (const Scenes::Scene::RenderItem& item : items) {
        if (classify(result.frustum, item) == Classification::Outside) {
            result.culled.push_back(item.entity);
            result.culled_triangles += triangleCount(item);
        } else {
            result.visible.push_back(item.entity);
            result.visible_triangles += triangleCount(item);
            out.push_back(item);
        }
    }
    return result;
}

Result System::buildEntityMask(
    const Ecs::World& world,
    int width,
    int height,
    std::vector<std::uint32_t>& out) const
{
    std::vector<Scenes::Scene::RenderItem> visible_items;
    Result result = collectVisibleRenderItems(world, width, height, visible_items);

    std::size_t size = 1u;
    for (const Ecs::Entity entity : world.entities())
        size = std::max(size, static_cast<std::size_t>(entity) + 1u);

    out.assign(size, 0u);
    for (const Scenes::Scene::RenderItem& item : visible_items) {
        if (static_cast<std::size_t>(item.entity) < out.size()) out[item.entity] = 1u;
    }
    return result;
}

std::array<Vec3, 8> System::corners(const Frustum& frustum) const
{
    std::array<Vec3, 8> result{};
    if (!frustum.valid) return result;

    float near_half_width = frustum.half_width;
    float near_half_height = frustum.half_height;
    float far_half_width = frustum.half_width;
    float far_half_height = frustum.half_height;
    if (!frustum.orthographic) {
        const float tangent_vertical = std::tan(frustum.half_vertical_fov_radians);
        const float tangent_horizontal = tangent_vertical * frustum.aspect;
        near_half_width = tangent_horizontal * frustum.near_distance;
        near_half_height = tangent_vertical * frustum.near_distance;
        far_half_width = tangent_horizontal * frustum.far_distance;
        far_half_height = tangent_vertical * frustum.far_distance;
    }

    const auto fill = [&](std::size_t offset, float distance, float half_width, float half_height) {
        const Vec3 center = add(frustum.position, multiply(frustum.forward, distance));
        const Vec3 horizontal = multiply(frustum.right, half_width);
        const Vec3 vertical = multiply(frustum.up, half_height);
        result[offset + 0u] = subtract(subtract(center, horizontal), vertical);
        result[offset + 1u] = add(subtract(center, vertical), horizontal);
        result[offset + 2u] = add(add(center, horizontal), vertical);
        result[offset + 3u] = add(subtract(center, horizontal), vertical);
    };

    fill(0u, frustum.near_distance, near_half_width, near_half_height);
    fill(4u, frustum.far_distance, far_half_width, far_half_height);
    return result;
}

} // namespace Renderer::Visibility
