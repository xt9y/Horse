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

bool aabbHit(
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
            if (o < low || o > high) return false;
            continue;
        }

        const float inverse = 1.0f / d;
        float t0 = (low - o) * inverse;
        float t1 = (high - o) * inverse;
        if (t0 > t1) std::swap(t0, t1);
        minimum_t = std::max(minimum_t, t0);
        maximum_t = std::min(maximum_t, t1);
        if (maximum_t < minimum_t) return false;
    }
    return minimum_t < maximum_distance;
}

bool triangleHit(
    const Scenes::GpuTriangle& triangle,
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

    const auto linear = [](std::uint8_t value) {
        return std::pow(static_cast<float>(value) / 255.0f, 2.2f);
    };
    return {
        linear(asset->image.rgba[offset + 0u]),
        linear(asset->image.rgba[offset + 1u]),
        linear(asset->image.rgba[offset + 2u]),
    };
}

} // namespace

bool TraceScene::build(
    const Ecs::World& world,
    const std::vector<Scenes::Scene::RenderItem>& items,
    std::string *error)
{
    return cache_.sync(
        world,
        items,
        std::numeric_limits<std::size_t>::max(),
        error
    );
}

TraceHit TraceScene::traceClosest(
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon) const
{
    TraceHit hit;
    hit.distance = maximum_distance;
    if (cache_.nodes().empty() || cache_.triangles().empty()) return hit;

    direction = normalize(direction);
    std::array<std::uint32_t, 64> stack{};
    std::size_t stack_size = 0u;
    stack[stack_size++] = 0u;

    while (stack_size > 0u) {
        const std::uint32_t node_index = stack[--stack_size];
        if (node_index >= cache_.nodes().size()) continue;
        const Scenes::GpuNode& node = cache_.nodes()[node_index];
        if (!aabbHit(node, origin, direction, hit.distance)) continue;

        if ((node.meta & Scenes::LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~Scenes::LeafBit;
            for (std::uint32_t index = 0u; index < count; ++index) {
                const std::uint32_t triangle_index = node.first + index;
                if (triangle_index >= cache_.triangles().size()) break;
                const Scenes::GpuTriangle& triangle = cache_.triangles()[triangle_index];
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
                    v
                )) continue;

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

        if (stack_size + 2u > stack.size()) continue;
        stack[stack_size++] = node.first;
        stack[stack_size++] = node.meta;
    }
    return hit;
}

bool TraceScene::occluded(
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float ray_epsilon) const
{
    const TraceHit hit = traceClosest(origin, direction, maximum_distance, ray_epsilon);
    return hit.found && hit.distance < maximum_distance;
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
    if (cache_.nodes().empty()) return result;
    const Scenes::GpuNode& root = cache_.nodes().front();
    result.minimum = {root.min_x, root.min_y, root.min_z};
    result.maximum = {root.max_x, root.max_y, root.max_z};
    result.valid = true;
    return result;
}

} // namespace Renderer::GlobalIllumination
