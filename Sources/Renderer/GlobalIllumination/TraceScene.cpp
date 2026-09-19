#include "Renderer/GlobalIllumination/TraceScene.hpp"

#include "Models/Core/Texture.hpp"
#include "Renderer/Math.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace Renderer::GlobalIllumination {
namespace {

float component(Vec3 value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

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

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(Vec3 value)
{
    const float length_squared = dot(value, value);
    if (length_squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    return multiply(value, 1.0f / std::sqrt(length_squared));
}

float srgbToLinear(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float aabbDistance(
    const Scenes::GpuNode& node,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance)
{
    float minimum_t = 0.0f;
    float maximum_t = maximum_distance;
    const Vec3 minimum{node.min_x, node.min_y, node.min_z};
    const Vec3 maximum{node.max_x, node.max_y, node.max_z};

    for (int axis = 0; axis < 3; ++axis) {
        const float o = component(origin, axis);
        const float d = component(direction, axis);
        const float low = component(minimum, axis);
        const float high = component(maximum, axis);
        if (std::abs(d) <= 1.0e-10f) {
            if (o < low || o > high) return std::numeric_limits<float>::infinity();
            continue;
        }

        const float inverse = 1.0f / d;
        float t0 = (low - o) * inverse;
        float t1 = (high - o) * inverse;
        if (t0 > t1) std::swap(t0, t1);
        minimum_t = std::max(minimum_t, t0);
        maximum_t = std::min(maximum_t, t1);
        if (maximum_t < minimum_t) return std::numeric_limits<float>::infinity();
    }
    return minimum_t < maximum_distance
        ? minimum_t
        : std::numeric_limits<float>::infinity();
}

template <typename Triangle>
bool triangleHit(
    const Triangle& triangle,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon,
    float& distance,
    float& u,
    float& v)
{
    const Vec3 p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
    const Vec3 p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
    const Vec3 p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
    const Vec3 edge1 = subtract(p1, p0);
    const Vec3 edge2 = subtract(p2, p0);
    const Vec3 p = cross(direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= 1.0e-9f) return false;

    const float inverse_determinant = 1.0f / determinant;
    const Vec3 offset = subtract(origin, p0);
    u = dot(offset, p) * inverse_determinant;
    if (u < 0.0f || u > 1.0f) return false;

    const Vec3 q = cross(offset, edge1);
    v = dot(direction, q) * inverse_determinant;
    if (v < 0.0f || u + v > 1.0f) return false;

    distance = dot(edge2, q) * inverse_determinant;
    return distance > ray_epsilon && distance < maximum_distance;
}

void pushChildrenNearFirst(
    const std::vector<Scenes::GpuNode>& nodes,
    const Scenes::GpuNode& node,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    std::array<std::uint32_t, 64>& stack,
    std::size_t& stack_size)
{
    if (node.first >= nodes.size() || node.meta >= nodes.size()) return;

    const float left_distance = aabbDistance(
        nodes[node.first],
        origin,
        direction,
        maximum_distance);
    const float right_distance = aabbDistance(
        nodes[node.meta],
        origin,
        direction,
        maximum_distance);
    const bool left_hit = std::isfinite(left_distance);
    const bool right_hit = std::isfinite(right_distance);

    if (left_hit && right_hit) {
        if (stack_size + 2u > stack.size()) return;
        if (left_distance <= right_distance) {
            stack[stack_size++] = node.meta;
            stack[stack_size++] = node.first;
        } else {
            stack[stack_size++] = node.first;
            stack[stack_size++] = node.meta;
        }
        return;
    }

    if (stack_size >= stack.size()) return;
    if (left_hit) stack[stack_size++] = node.first;
    else if (right_hit) stack[stack_size++] = node.meta;
}

TraceHit traceLegacyClosest(
    const Scenes::SceneCache& cache,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon)
{
    TraceHit hit;
    hit.distance = maximum_distance;
    if (cache.nodes().empty() || cache.triangles().empty()) return hit;

    std::array<std::uint32_t, 64> stack{};
    std::size_t stack_size = 0u;
    stack[stack_size++] = 0u;

    while (stack_size > 0u) {
        const std::uint32_t node_index = stack[--stack_size];
        if (node_index >= cache.nodes().size()) continue;
        const Scenes::GpuNode& node = cache.nodes()[node_index];
        if (!std::isfinite(aabbDistance(node, origin, direction, hit.distance))) continue;

        if ((node.meta & Scenes::LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~Scenes::LeafBit;
            for (std::uint32_t index = 0u; index < count; ++index) {
                const std::uint32_t triangle_index = node.first + index;
                if (triangle_index >= cache.triangles().size()) break;
                const Scenes::GpuTriangle& triangle = cache.triangles()[triangle_index];
                float distance = hit.distance;
                float u = 0.0f;
                float v = 0.0f;
                if (!triangleHit(
                        triangle,
                        origin,
                        direction,
                        hit.distance,
                        ray_epsilon,
                        distance,
                        u,
                        v))
                    continue;

                const float w = 1.0f - u - v;
                Vec3 normal{
                    triangle.n0[0] * w + triangle.n1[0] * u + triangle.n2[0] * v,
                    triangle.n0[1] * w + triangle.n1[1] * u + triangle.n2[1] * v,
                    triangle.n0[2] * w + triangle.n1[2] * u + triangle.n2[2] * v,
                };
                normal = normalize(normal);
                if (dot(normal, direction) > 0.0f) normal = multiply(normal, -1.0f);

                hit.found = true;
                hit.distance = distance;
                hit.position = add(origin, multiply(direction, distance));
                hit.normal = normal;
                hit.uv = {
                    triangle.uv01[0] * w + triangle.uv01[2] * u + triangle.uv2[0] * v,
                    triangle.uv01[1] * w + triangle.uv01[3] * u + triangle.uv2[1] * v,
                };
                hit.triangle = triangle_index;
                hit.material = std::bit_cast<std::uint32_t>(triangle.p0[3]);
            }
            continue;
        }

        pushChildrenNearFirst(
            cache.nodes(),
            node,
            origin,
            direction,
            hit.distance,
            stack,
            stack_size);
    }
    return hit;
}

TraceHit traceAccelerationClosest(
    const Scenes::AccelerationScene& acceleration,
    const Scenes::SceneCache& resources,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon)
{
    TraceHit hit;
    hit.distance = maximum_distance;
    if (acceleration.tlasNodes().empty() || acceleration.instances().empty()) return hit;

    std::array<std::uint32_t, 64> tlas_stack{};
    std::size_t tlas_stack_size = 0u;
    tlas_stack[tlas_stack_size++] = 0u;

    while (tlas_stack_size > 0u) {
        const std::uint32_t node_index = tlas_stack[--tlas_stack_size];
        if (node_index >= acceleration.tlasNodes().size()) continue;
        const Scenes::GpuNode& node = acceleration.tlasNodes()[node_index];
        if (!std::isfinite(aabbDistance(node, origin, direction, hit.distance))) continue;

        if ((node.meta & Scenes::LeafBit) == 0u) {
            pushChildrenNearFirst(
                acceleration.tlasNodes(),
                node,
                origin,
                direction,
                hit.distance,
                tlas_stack,
                tlas_stack_size);
            continue;
        }

        const std::uint32_t instance_count = node.meta & ~Scenes::LeafBit;
        for (std::uint32_t local_instance = 0u; local_instance < instance_count; ++local_instance) {
            const std::uint32_t instance_index = node.first + local_instance;
            if (instance_index >= acceleration.instances().size()) break;
            const Scenes::AccelerationInstance& instance = acceleration.instances()[instance_index];
            if (instance.blasIndex() >= acceleration.blases().size()) continue;
            const Scenes::AccelerationBlas& blas = acceleration.blases()[instance.blasIndex()];
            if (blas.node_count == 0u || blas.triangle_count == 0u) continue;

            const Vec3 local_origin = Math::transformPoint(instance.world_to_object, origin);
            const Vec3 local_direction = Math::transformVector(instance.world_to_object, direction);
            std::array<std::uint32_t, 64> blas_stack{};
            std::size_t blas_stack_size = 0u;
            blas_stack[blas_stack_size++] = blas.node_offset;

            while (blas_stack_size > 0u) {
                const std::uint32_t blas_node_index = blas_stack[--blas_stack_size];
                if (blas_node_index >= acceleration.blasNodes().size()) continue;
                const Scenes::GpuNode& blas_node = acceleration.blasNodes()[blas_node_index];
                if (!std::isfinite(aabbDistance(
                        blas_node,
                        local_origin,
                        local_direction,
                        hit.distance)))
                    continue;

                if ((blas_node.meta & Scenes::LeafBit) == 0u) {
                    pushChildrenNearFirst(
                        acceleration.blasNodes(),
                        blas_node,
                        local_origin,
                        local_direction,
                        hit.distance,
                        blas_stack,
                        blas_stack_size);
                    continue;
                }

                const std::uint32_t triangle_count = blas_node.meta & ~Scenes::LeafBit;
                for (std::uint32_t local_triangle = 0u; local_triangle < triangle_count; ++local_triangle) {
                    const std::uint32_t triangle_index = blas_node.first + local_triangle;
                    if (triangle_index >= acceleration.localTriangles().size()) break;
                    const Scenes::AccelerationTriangle& triangle =
                        acceleration.localTriangles()[triangle_index];
                    float distance = hit.distance;
                    float u = 0.0f;
                    float v = 0.0f;
                    if (!triangleHit(
                            triangle,
                            local_origin,
                            local_direction,
                            hit.distance,
                            ray_epsilon,
                            distance,
                            u,
                            v))
                        continue;

                    const float w = 1.0f - u - v;
                    Vec3 local_normal{
                        triangle.n0[0] * w + triangle.n1[0] * u + triangle.n2[0] * v,
                        triangle.n0[1] * w + triangle.n1[1] * u + triangle.n2[1] * v,
                        triangle.n0[2] * w + triangle.n1[2] * u + triangle.n2[2] * v,
                    };
                    Vec3 normal = normalize(Math::transformNormal(
                        instance.world_to_object,
                        local_normal));
                    if (dot(normal, direction) > 0.0f) normal = multiply(normal, -1.0f);

                    hit.found = true;
                    hit.distance = distance;
                    hit.position = add(origin, multiply(direction, distance));
                    hit.normal = normal;
                    hit.uv = {
                        triangle.uv01[0] * w + triangle.uv01[2] * u + triangle.uv2[0] * v,
                        triangle.uv01[1] * w + triangle.uv01[3] * u + triangle.uv2[1] * v,
                    };
                    hit.triangle = triangle_index;
                    hit.material = resources.materialIndex(instance.material());
                }
            }
        }
    }
    return hit;
}

bool legacyOccluded(
    const Scenes::SceneCache& cache,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon)
{
    if (cache.nodes().empty() || cache.triangles().empty()) return false;

    std::array<std::uint32_t, 64> stack{};
    std::size_t stack_size = 0u;
    stack[stack_size++] = 0u;
    while (stack_size > 0u) {
        const std::uint32_t node_index = stack[--stack_size];
        if (node_index >= cache.nodes().size()) continue;
        const Scenes::GpuNode& node = cache.nodes()[node_index];
        if (!std::isfinite(aabbDistance(node, origin, direction, maximum_distance))) continue;

        if ((node.meta & Scenes::LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~Scenes::LeafBit;
            for (std::uint32_t index = 0u; index < count; ++index) {
                const std::uint32_t triangle_index = node.first + index;
                if (triangle_index >= cache.triangles().size()) break;
                float distance = maximum_distance;
                float u = 0.0f;
                float v = 0.0f;
                if (triangleHit(
                        cache.triangles()[triangle_index],
                        origin,
                        direction,
                        maximum_distance,
                        ray_epsilon,
                        distance,
                        u,
                        v))
                    return true;
            }
            continue;
        }

        pushChildrenNearFirst(
            cache.nodes(),
            node,
            origin,
            direction,
            maximum_distance,
            stack,
            stack_size);
    }
    return false;
}

bool accelerationOccluded(
    const Scenes::AccelerationScene& acceleration,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon)
{
    if (acceleration.tlasNodes().empty() || acceleration.instances().empty()) return false;

    std::array<std::uint32_t, 64> tlas_stack{};
    std::size_t tlas_stack_size = 0u;
    tlas_stack[tlas_stack_size++] = 0u;
    while (tlas_stack_size > 0u) {
        const std::uint32_t node_index = tlas_stack[--tlas_stack_size];
        if (node_index >= acceleration.tlasNodes().size()) continue;
        const Scenes::GpuNode& node = acceleration.tlasNodes()[node_index];
        if (!std::isfinite(aabbDistance(node, origin, direction, maximum_distance))) continue;

        if ((node.meta & Scenes::LeafBit) == 0u) {
            pushChildrenNearFirst(
                acceleration.tlasNodes(),
                node,
                origin,
                direction,
                maximum_distance,
                tlas_stack,
                tlas_stack_size);
            continue;
        }

        const std::uint32_t instance_count = node.meta & ~Scenes::LeafBit;
        for (std::uint32_t local_instance = 0u; local_instance < instance_count; ++local_instance) {
            const std::uint32_t instance_index = node.first + local_instance;
            if (instance_index >= acceleration.instances().size()) break;
            const Scenes::AccelerationInstance& instance = acceleration.instances()[instance_index];
            if (instance.blasIndex() >= acceleration.blases().size()) continue;
            const Scenes::AccelerationBlas& blas = acceleration.blases()[instance.blasIndex()];
            if (blas.node_count == 0u || blas.triangle_count == 0u) continue;

            const Vec3 local_origin = Math::transformPoint(instance.world_to_object, origin);
            const Vec3 local_direction = Math::transformVector(instance.world_to_object, direction);
            std::array<std::uint32_t, 64> blas_stack{};
            std::size_t blas_stack_size = 0u;
            blas_stack[blas_stack_size++] = blas.node_offset;
            while (blas_stack_size > 0u) {
                const std::uint32_t blas_node_index = blas_stack[--blas_stack_size];
                if (blas_node_index >= acceleration.blasNodes().size()) continue;
                const Scenes::GpuNode& blas_node = acceleration.blasNodes()[blas_node_index];
                if (!std::isfinite(aabbDistance(
                        blas_node,
                        local_origin,
                        local_direction,
                        maximum_distance)))
                    continue;

                if ((blas_node.meta & Scenes::LeafBit) != 0u) {
                    const std::uint32_t triangle_count = blas_node.meta & ~Scenes::LeafBit;
                    for (std::uint32_t local_triangle = 0u; local_triangle < triangle_count; ++local_triangle) {
                        const std::uint32_t triangle_index = blas_node.first + local_triangle;
                        if (triangle_index >= acceleration.localTriangles().size()) break;
                        float distance = maximum_distance;
                        float u = 0.0f;
                        float v = 0.0f;
                        if (triangleHit(
                                acceleration.localTriangles()[triangle_index],
                                local_origin,
                                local_direction,
                                maximum_distance,
                                ray_epsilon,
                                distance,
                                u,
                                v))
                            return true;
                    }
                    continue;
                }

                pushChildrenNearFirst(
                    acceleration.blasNodes(),
                    blas_node,
                    local_origin,
                    local_direction,
                    maximum_distance,
                    blas_stack,
                    blas_stack_size);
            }
        }
    }
    return false;
}

Vec3 textureColor(const Scenes::SceneCache& cache, int texture_index, Vec2 uv)
{
    if (texture_index < 0 || static_cast<std::size_t>(texture_index) >= cache.textureHandles().size()) {
        return {1.0f, 1.0f, 1.0f};
    }

    const Models::TextureAsset *asset = Models::texture(
        cache.textureHandles()[static_cast<std::size_t>(texture_index)]
    );
    if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
        return {1.0f, 1.0f, 1.0f};
    }

    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);
    if (u < 0.0f) u += 1.0f;
    if (v < 0.0f) v += 1.0f;

    const int x = std::clamp(
        static_cast<int>(u * static_cast<float>(asset->image.width)),
        0,
        asset->image.width - 1
    );
    const int y = std::clamp(
        static_cast<int>((1.0f - v) * static_cast<float>(asset->image.height)),
        0,
        asset->image.height - 1
    );
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(asset->image.width) +
         static_cast<std::size_t>(x)) * 4u;
    if (offset + 2u >= asset->image.rgba.size()) return {1.0f, 1.0f, 1.0f};

    return {
        srgbToLinear(static_cast<float>(asset->image.rgba[offset + 0u]) / 255.0f),
        srgbToLinear(static_cast<float>(asset->image.rgba[offset + 1u]) / 255.0f),
        srgbToLinear(static_cast<float>(asset->image.rgba[offset + 2u]) / 255.0f),
    };
}

} // namespace

bool TraceScene::build(
    const Ecs::World& world,
    const std::vector<Scenes::Scene::RenderItem>& items,
    std::string *error)
{
    if (!cache_.syncResources(
            world,
            items,
            std::numeric_limits<std::size_t>::max(),
            error))
        return false;

    std::vector<Scenes::Scene::RenderItem> dynamic_items;
    dynamic_items.reserve(items.size());
    for (const Scenes::Scene::RenderItem& item : items) {
        if (!Scenes::AccelerationScene::eligible(world, item))
            dynamic_items.push_back(item);
    }

    if (!cache_.syncGeometry(world, dynamic_items, error)) return false;
    return acceleration_.sync(world, items, error);
}

TraceHit TraceScene::traceClosest(
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon) const
{
    direction = normalize(direction);
    TraceHit hit = traceLegacyClosest(
        cache_,
        origin,
        direction,
        maximum_distance,
        ray_epsilon);
    const TraceHit accelerated = traceAccelerationClosest(
        acceleration_,
        cache_,
        origin,
        direction,
        hit.distance,
        ray_epsilon);
    if (accelerated.found && (!hit.found || accelerated.distance < hit.distance))
        return accelerated;
    return hit;
}

bool TraceScene::occluded(
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon) const
{
    direction = normalize(direction);
    if (legacyOccluded(cache_, origin, direction, maximum_distance, ray_epsilon)) return true;
    return accelerationOccluded(
        acceleration_,
        origin,
        direction,
        maximum_distance,
        ray_epsilon);
}

Vec3 TraceScene::albedo(const TraceHit& hit) const
{
    if (!hit.found || hit.material >= cache_.materials().size()) {
        return {1.0f, 1.0f, 1.0f};
    }

    const Scenes::GpuMaterial& material = cache_.materials()[hit.material];
    Vec3 result{
        std::max(material.base_color[0], 0.0f),
        std::max(material.base_color[1], 0.0f),
        std::max(material.base_color[2], 0.0f),
    };
    const Vec3 texture = textureColor(cache_, material.data[0], hit.uv);
    result = {result.x * texture.x, result.y * texture.y, result.z * texture.z};
    return {
        std::clamp(result.x, 0.0f, 1.0f),
        std::clamp(result.y, 0.0f, 1.0f),
        std::clamp(result.z, 0.0f, 1.0f),
    };
}

TraceBounds TraceScene::bounds() const
{
    TraceBounds result;
    const auto include = [&](const Scenes::GpuNode& root, TraceBounds *bounds) {
        const Vec3 minimum{root.min_x, root.min_y, root.min_z};
        const Vec3 maximum{root.max_x, root.max_y, root.max_z};
        if (!bounds->valid) {
            bounds->minimum = minimum;
            bounds->maximum = maximum;
            bounds->valid = true;
            return;
        }
        bounds->minimum = {
            std::min(bounds->minimum.x, minimum.x),
            std::min(bounds->minimum.y, minimum.y),
            std::min(bounds->minimum.z, minimum.z),
        };
        bounds->maximum = {
            std::max(bounds->maximum.x, maximum.x),
            std::max(bounds->maximum.y, maximum.y),
            std::max(bounds->maximum.z, maximum.z),
        };
    };

    if (!cache_.nodes().empty()) include(cache_.nodes().front(), &result);
    if (!acceleration_.tlasNodes().empty()) include(acceleration_.tlasNodes().front(), &result);
    return result;
}

} // namespace Renderer::GlobalIllumination
