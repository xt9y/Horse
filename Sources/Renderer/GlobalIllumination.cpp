#include "Renderer/GlobalIllumination.hpp"

#include "Animation/Animation.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace Renderer::GlobalIllumination {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kRayEpsilon = 0.0025f;
constexpr std::size_t kLeafSize = 8u;
constexpr std::size_t kMaximumTriangles = 1000000u;
constexpr std::size_t kRaysPerProbe = 48u;
constexpr std::size_t kProbeBudgetPerFrame = 16u;
constexpr float kY00 = 0.2820947918f;
constexpr float kY1 = 0.4886025119f;

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(Vec3 a, Vec3 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 divide(Vec3 value, float scalar)
{
    if (std::abs(scalar) <= 1.0e-20f) return {};
    return multiply(value, 1.0f / scalar);
}

Vec3 minVec(Vec3 a, Vec3 b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec(Vec3 a, Vec3 b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
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

float lengthSquared(Vec3 value)
{
    return dot(value, value);
}

float length(Vec3 value)
{
    return std::sqrt(lengthSquared(value));
}

Vec3 normalize(Vec3 value)
{
    const float magnitude = length(value);
    if (magnitude <= 1.0e-12f) return {0.0f, 1.0f, 0.0f};
    return divide(value, magnitude);
}

float component(Vec3 value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

Vec3 clampPositive(Vec3 value)
{
    return {
        std::max(value.x, 0.0f),
        std::max(value.y, 0.0f),
        std::max(value.z, 0.0f),
    };
}

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hashValue(hash, static_cast<std::uint32_t>(value));
    hashValue(hash, static_cast<std::uint32_t>(value >> 32u));
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

void hashVec3(std::uint64_t& hash, Vec3 value)
{
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashTransform(std::uint64_t& hash, const Transform& transform)
{
    hashVec3(hash, transform.position);
    hashVec3(hash, transform.rotation);
    hashVec3(hash, transform.scale);
}

struct Aabb {
    Vec3 minimum {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    Vec3 maximum {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };

    void include(Vec3 point)
    {
        minimum = minVec(minimum, point);
        maximum = maxVec(maximum, point);
    }

    void include(const Aabb& other)
    {
        minimum = minVec(minimum, other.minimum);
        maximum = maxVec(maximum, other.maximum);
    }
};

struct Triangle {
    Vec3 p0{};
    Vec3 p1{};
    Vec3 p2{};
    Vec2 uv0{};
    Vec2 uv1{};
    Vec2 uv2{};
    Vec3 normal {0.0f, 1.0f, 0.0f};
    Vec3 base_color {1.0f, 1.0f, 1.0f};
    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    Aabb bounds{};
};

struct Node {
    Aabb bounds{};
    std::uint32_t left = 0u;
    std::uint32_t right = 0u;
    std::uint32_t first = 0u;
    std::uint32_t count = 0u;
};

struct Hit {
    bool found = false;
    float distance = std::numeric_limits<float>::infinity();
    Vec3 position{};
    Vec3 normal {0.0f, 1.0f, 0.0f};
    Vec2 uv{};
    std::uint32_t triangle = 0u;
};

struct SettingsState {
    bool valid = false;
    GlobalIlluminationComponent component{};
};

struct State {
    const Ecs::World *world = nullptr;
    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t geometry_signature = 0u;
    std::uint64_t light_signature = 0u;
    std::uint8_t bounces = 0u;
    std::uint64_t next_field_revision = 1u;

    Scene::LightState light{};
    std::vector<Scene::RenderItem> render_items;
    std::vector<Triangle> triangles;
    std::vector<std::uint32_t> triangle_order;
    std::vector<Node> nodes;
    Aabb scene_bounds{};

    Field published{};
    Field working{};
    Field source{};
    std::size_t probe_cursor = 0u;
    std::uint8_t bounce_index = 0u;
    bool calculating = false;
};

State state;

SettingsState settingsState(const Ecs::World& world)
{
    SettingsState result;
    for (const Ecs::Entity entity : world.entities()) {
        const GlobalIlluminationComponent *component =
            world.get<GlobalIlluminationComponent>(entity);
        if (!component) continue;
        result.valid = component->enabled;
        result.component = *component;
        break;
    }
    return result;
}

std::uint64_t geometrySignature(const Ecs::World& world, const std::vector<Scene::RenderItem>& items)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, static_cast<std::uint64_t>(items.size()));

    for (const Scene::RenderItem& item : items) {
        hashValue(hash, static_cast<std::uint32_t>(item.entity));
        if (item.mesh_component) {
            hashValue(hash, item.mesh_component->mesh);
            hashValue(hash, item.mesh_component->material);
        }
        if (item.transform) hashTransform(hash, *item.transform);

        const Animation::SkinBindingComponent *binding =
            world.get<Animation::SkinBindingComponent>(item.entity);
        if (binding && binding->animator != Ecs::INVALID_ENTITY) {
            const Animation::AnimatorComponent *animator =
                world.get<Animation::AnimatorComponent>(binding->animator);
            if (animator) hashValue(hash, animator->pose.revision);
        }
    }
    return hash;
}

std::uint64_t lightSignature(const Scene::LightState& light)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, light.valid ? 1u : 0u);
    if (!light.valid) return hash;
    hashValue(hash, static_cast<std::uint32_t>(light.light.type));
    hashVec3(hash, light.light.color);
    hashFloat(hash, light.light.intensity);
    hashTransform(hash, light.transform);
    return hash;
}

Vec3 triangleCentroid(const Triangle& triangle)
{
    return divide(add(add(triangle.p0, triangle.p1), triangle.p2), 3.0f);
}

std::uint32_t buildNode(std::uint32_t first, std::uint32_t count)
{
    const std::uint32_t node_index = static_cast<std::uint32_t>(state.nodes.size());
    state.nodes.push_back(Node{});

    Aabb bounds;
    Aabb centroid_bounds;
    for (std::uint32_t i = 0u; i < count; ++i) {
        const Triangle& triangle = state.triangles[state.triangle_order[first + i]];
        bounds.include(triangle.bounds);
        centroid_bounds.include(triangleCentroid(triangle));
    }

    if (count <= kLeafSize) {
        Node& node = state.nodes[node_index];
        node.bounds = bounds;
        node.first = first;
        node.count = count;
        return node_index;
    }

    const Vec3 extent = subtract(centroid_bounds.maximum, centroid_bounds.minimum);
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (component(extent, 2) > component(extent, axis)) axis = 2;

    const std::uint32_t middle = first + count / 2u;
    std::nth_element(
        state.triangle_order.begin() + first,
        state.triangle_order.begin() + middle,
        state.triangle_order.begin() + first + count,
        [axis](std::uint32_t a, std::uint32_t b) {
            return component(triangleCentroid(state.triangles[a]), axis) <
                component(triangleCentroid(state.triangles[b]), axis);
        }
    );

    const std::uint32_t left = buildNode(first, middle - first);
    const std::uint32_t right = buildNode(middle, first + count - middle);
    Node& node = state.nodes[node_index];
    node.bounds = bounds;
    node.left = left;
    node.right = right;
    return node_index;
}

bool aabbHit(const Aabb& bounds, Vec3 origin, Vec3 direction, float maximum_distance)
{
    float minimum_t = 0.0f;
    float maximum_t = maximum_distance;

    for (int axis = 0; axis < 3; ++axis) {
        const float o = component(origin, axis);
        const float d = component(direction, axis);
        const float minimum = component(bounds.minimum, axis);
        const float maximum = component(bounds.maximum, axis);

        if (std::abs(d) <= 1.0e-10f) {
            if (o < minimum || o > maximum) return false;
            continue;
        }

        const float inverse = 1.0f / d;
        float t0 = (minimum - o) * inverse;
        float t1 = (maximum - o) * inverse;
        if (t0 > t1) std::swap(t0, t1);
        minimum_t = std::max(minimum_t, t0);
        maximum_t = std::min(maximum_t, t1);
        if (maximum_t < minimum_t) return false;
    }
    return minimum_t < maximum_distance;
}

bool triangleHit(
    const Triangle& triangle,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    float *distance,
    float *u,
    float *v)
{
    const Vec3 edge1 = subtract(triangle.p1, triangle.p0);
    const Vec3 edge2 = subtract(triangle.p2, triangle.p0);
    const Vec3 p = cross(direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= 1.0e-9f) return false;

    const float inverse_determinant = 1.0f / determinant;
    const Vec3 offset = subtract(origin, triangle.p0);
    const float bary_u = dot(offset, p) * inverse_determinant;
    if (bary_u < 0.0f || bary_u > 1.0f) return false;

    const Vec3 q = cross(offset, edge1);
    const float bary_v = dot(direction, q) * inverse_determinant;
    if (bary_v < 0.0f || bary_u + bary_v > 1.0f) return false;

    const float t = dot(edge2, q) * inverse_determinant;
    if (t <= kRayEpsilon || t >= maximum_distance) return false;

    *distance = t;
    *u = bary_u;
    *v = bary_v;
    return true;
}

Hit traceClosest(Vec3 origin, Vec3 direction, float maximum_distance)
{
    Hit hit;
    hit.distance = maximum_distance;
    if (state.nodes.empty()) return hit;

    std::array<std::uint32_t, 64> stack{};
    std::size_t stack_size = 0u;
    stack[stack_size++] = 0u;

    while (stack_size > 0u) {
        const std::uint32_t node_index = stack[--stack_size];
        if (node_index >= state.nodes.size()) continue;
        const Node& node = state.nodes[node_index];
        if (!aabbHit(node.bounds, origin, direction, hit.distance)) continue;

        if (node.count > 0u) {
            for (std::uint32_t i = 0u; i < node.count; ++i) {
                const std::uint32_t triangle_index = state.triangle_order[node.first + i];
                const Triangle& triangle = state.triangles[triangle_index];
                float distance = hit.distance;
                float u = 0.0f;
                float v = 0.0f;
                if (!triangleHit(triangle, origin, direction, hit.distance, &distance, &u, &v)) continue;

                const float w = 1.0f - u - v;
                hit.found = true;
                hit.distance = distance;
                hit.position = add(origin, multiply(direction, distance));
                hit.normal = triangle.normal;
                if (dot(hit.normal, direction) > 0.0f) hit.normal = multiply(hit.normal, -1.0f);
                hit.uv = {
                    triangle.uv0.x * w + triangle.uv1.x * u + triangle.uv2.x * v,
                    triangle.uv0.y * w + triangle.uv1.y * u + triangle.uv2.y * v,
                };
                hit.triangle = triangle_index;
            }
            continue;
        }

        if (stack_size + 2u > stack.size()) continue;
        stack[stack_size++] = node.left;
        stack[stack_size++] = node.right;
    }
    return hit;
}

bool occluded(Vec3 origin, Vec3 direction, float maximum_distance)
{
    const Hit hit = traceClosest(origin, direction, maximum_distance);
    return hit.found && hit.distance < maximum_distance;
}

Vec3 textureColor(Models::TextureHandle handle, Vec2 uv)
{
    const Models::TextureAsset *asset = Models::texture(handle);
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

Vec3 albedo(const Hit& hit)
{
    if (!hit.found || hit.triangle >= state.triangles.size()) return {1.0f, 1.0f, 1.0f};
    const Triangle& triangle = state.triangles[hit.triangle];
    Vec3 result = clampPositive(triangle.base_color);
    if (triangle.texture != Models::INVALID_TEXTURE) {
        result = multiply(result, textureColor(triangle.texture, hit.uv));
    }
    return {
        std::clamp(result.x, 0.0f, 1.0f),
        std::clamp(result.y, 0.0f, 1.0f),
        std::clamp(result.z, 0.0f, 1.0f),
    };
}

Vec3 directIrradiance(const Hit& hit)
{
    if (!state.light.valid || state.light.light.intensity <= 0.0f) return {};

    Vec3 light_direction{};
    float attenuation = 1.0f;
    float shadow_distance = std::numeric_limits<float>::infinity();

    if (state.light.light.type == LightType::Directional) {
        light_direction = normalize(state.light.transform.position);
        if (lengthSquared(state.light.transform.position) <= 1.0e-12f) {
            light_direction = normalize(Vec3{-0.35f, 0.8f, 0.45f});
        }
    } else {
        const Vec3 to_light = subtract(state.light.transform.position, hit.position);
        const float distance_squared = std::max(lengthSquared(to_light), 1.0e-4f);
        const float distance = std::sqrt(distance_squared);
        light_direction = divide(to_light, distance);
        attenuation = 1.0f / distance_squared;
        shadow_distance = std::max(distance - kRayEpsilon * 8.0f, 0.0f);
    }

    const float cosine = std::max(dot(hit.normal, light_direction), 0.0f);
    if (cosine <= 0.0f) return {};

    const Vec3 shadow_origin = add(hit.position, multiply(hit.normal, kRayEpsilon * 4.0f));
    if (occluded(shadow_origin, light_direction, shadow_distance)) return {};

    return multiply(
        clampPositive(state.light.light.color),
        std::max(state.light.light.intensity, 0.0f) * attenuation * cosine
    );
}

std::size_t probeIndex(const Field& field, std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return static_cast<std::size_t>(x) +
        static_cast<std::size_t>(field.size_x) *
        (static_cast<std::size_t>(y) +
         static_cast<std::size_t>(field.size_y) * static_cast<std::size_t>(z));
}

Vec3 lerp(Vec3 a, Vec3 b, float t)
{
    return add(multiply(a, 1.0f - t), multiply(b, t));
}

Vec3 sampleField(const Field *field, Vec3 position, Vec3 normal, bool apply_intensity)
{
    if (!field || !field->valid()) return {};

    const Vec3 span = subtract(field->maximum, field->minimum);
    const auto coordinate = [](float value, float minimum, float extent, std::uint32_t size) {
        if (extent <= 1.0e-8f || size <= 1u) return 0.0f;
        const float normalized = std::clamp((value - minimum) / extent, 0.0f, 1.0f);
        return normalized * static_cast<float>(size - 1u);
    };

    const float gx = coordinate(position.x, field->minimum.x, span.x, field->size_x);
    const float gy = coordinate(position.y, field->minimum.y, span.y, field->size_y);
    const float gz = coordinate(position.z, field->minimum.z, span.z, field->size_z);

    const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(gx));
    const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(gy));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::floor(gz));
    const std::uint32_t x1 = std::min(x0 + 1u, field->size_x - 1u);
    const std::uint32_t y1 = std::min(y0 + 1u, field->size_y - 1u);
    const std::uint32_t z1 = std::min(z0 + 1u, field->size_z - 1u);
    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);
    const float tz = gz - static_cast<float>(z0);

    std::array<Vec3, 4> coefficients{};
    for (std::size_t coefficient = 0u; coefficient < coefficients.size(); ++coefficient) {
        const Vec3 c000 = field->probes[probeIndex(*field, x0, y0, z0)].sh[coefficient];
        const Vec3 c100 = field->probes[probeIndex(*field, x1, y0, z0)].sh[coefficient];
        const Vec3 c010 = field->probes[probeIndex(*field, x0, y1, z0)].sh[coefficient];
        const Vec3 c110 = field->probes[probeIndex(*field, x1, y1, z0)].sh[coefficient];
        const Vec3 c001 = field->probes[probeIndex(*field, x0, y0, z1)].sh[coefficient];
        const Vec3 c101 = field->probes[probeIndex(*field, x1, y0, z1)].sh[coefficient];
        const Vec3 c011 = field->probes[probeIndex(*field, x0, y1, z1)].sh[coefficient];
        const Vec3 c111 = field->probes[probeIndex(*field, x1, y1, z1)].sh[coefficient];

        const Vec3 c00 = lerp(c000, c100, tx);
        const Vec3 c10 = lerp(c010, c110, tx);
        const Vec3 c01 = lerp(c001, c101, tx);
        const Vec3 c11 = lerp(c011, c111, tx);
        coefficients[coefficient] = lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
    }

    const Vec3 n = normalize(normal);
    Vec3 irradiance = multiply(coefficients[0], kPi * kY00);
    irradiance = add(irradiance, multiply(coefficients[1], (2.0f * kPi / 3.0f) * kY1 * n.x));
    irradiance = add(irradiance, multiply(coefficients[2], (2.0f * kPi / 3.0f) * kY1 * n.y));
    irradiance = add(irradiance, multiply(coefficients[3], (2.0f * kPi / 3.0f) * kY1 * n.z));
    irradiance = clampPositive(irradiance);
    return apply_intensity ? multiply(irradiance, std::max(field->intensity, 0.0f)) : irradiance;
}

Vec3 fibonacciDirection(std::size_t ray, std::size_t probe)
{
    constexpr float golden_angle = 2.39996322972865332f;
    const float t = (static_cast<float>(ray) + 0.5f) / static_cast<float>(kRaysPerProbe);
    const float z = 1.0f - 2.0f * t;
    const float radius = std::sqrt(std::max(1.0f - z * z, 0.0f));
    const float phase = golden_angle * (static_cast<float>(ray) + static_cast<float>(probe) * 0.61803398875f);
    return {radius * std::cos(phase), radius * std::sin(phase), z};
}

Vec3 probePosition(const Field& field, std::size_t index)
{
    const std::size_t plane = static_cast<std::size_t>(field.size_x) * field.size_y;
    const std::uint32_t z = static_cast<std::uint32_t>(index / plane);
    const std::size_t remaining = index - static_cast<std::size_t>(z) * plane;
    const std::uint32_t y = static_cast<std::uint32_t>(remaining / field.size_x);
    const std::uint32_t x = static_cast<std::uint32_t>(remaining % field.size_x);

    const auto axis = [](float minimum, float maximum, std::uint32_t coordinate, std::uint32_t size) {
        if (size <= 1u) return minimum;
        const float t = static_cast<float>(coordinate) / static_cast<float>(size - 1u);
        return minimum + (maximum - minimum) * t;
    };
    return {
        axis(field.minimum.x, field.maximum.x, x, field.size_x),
        axis(field.minimum.y, field.maximum.y, y, field.size_y),
        axis(field.minimum.z, field.maximum.z, z, field.size_z),
    };
}

void solveProbe(std::size_t probe_index)
{
    Probe result{};
    const Vec3 origin = probePosition(state.working, probe_index);
    const float weight = 4.0f * kPi / static_cast<float>(kRaysPerProbe);

    for (std::size_t ray = 0u; ray < kRaysPerProbe; ++ray) {
        const Vec3 direction = fibonacciDirection(ray, probe_index);
        const Hit hit = traceClosest(origin, direction, std::numeric_limits<float>::infinity());
        if (!hit.found) continue;

        Vec3 irradiance = directIrradiance(hit);
        if (state.bounce_index > 0u && state.source.valid()) {
            irradiance = add(irradiance, sampleField(&state.source, hit.position, hit.normal, false));
        }

        const Vec3 outgoing = multiply(multiply(albedo(hit), irradiance), 1.0f / kPi);
        const std::array<float, 4> basis {
            kY00,
            kY1 * direction.x,
            kY1 * direction.y,
            kY1 * direction.z,
        };
        for (std::size_t coefficient = 0u; coefficient < result.sh.size(); ++coefficient) {
            result.sh[coefficient] = add(
                result.sh[coefficient],
                multiply(outgoing, basis[coefficient] * weight)
            );
        }
    }

    state.working.probes[probe_index] = result;
}

std::uint32_t dimensionFor(float extent, float maximum_extent)
{
    if (maximum_extent <= 1.0e-6f) return 3u;
    const float ratio = std::clamp(extent / maximum_extent, 0.0f, 1.0f);
    return static_cast<std::uint32_t>(std::clamp(std::lround(3.0 + ratio * 5.0), 3l, 8l));
}

void configureFields(float intensity)
{
    const Vec3 raw_extent = subtract(state.scene_bounds.maximum, state.scene_bounds.minimum);
    const float maximum_extent = std::max({raw_extent.x, raw_extent.y, raw_extent.z, 1.0e-3f});
    const float margin = std::max(maximum_extent * 0.05f, 0.25f);

    Field field;
    field.minimum = subtract(state.scene_bounds.minimum, Vec3{margin, margin, margin});
    field.maximum = add(state.scene_bounds.maximum, Vec3{margin, margin, margin});
    const Vec3 extent = subtract(field.maximum, field.minimum);
    field.size_x = dimensionFor(extent.x, std::max({extent.x, extent.y, extent.z}));
    field.size_y = dimensionFor(extent.y, std::max({extent.x, extent.y, extent.z}));
    field.size_z = dimensionFor(extent.z, std::max({extent.x, extent.y, extent.z}));
    field.intensity = intensity;
    field.revision = state.next_field_revision++;
    field.probes.resize(field.probeCount());

    state.published = field;
    state.working = field;
    state.working.intensity = 1.0f;
    state.source = {};
    state.probe_cursor = 0u;
    state.bounce_index = 0u;
    state.calculating = field.valid();
}

void restartCalculation(float intensity)
{
    if (state.triangles.empty()) {
        state.published = {};
        state.working = {};
        state.source = {};
        state.calculating = false;
        return;
    }
    configureFields(intensity);
}

void buildGeometry(const Ecs::World& world, const std::vector<Scene::RenderItem>& items)
{
    state.triangles.clear();
    state.triangle_order.clear();
    state.nodes.clear();
    state.scene_bounds = {};
    state.triangles.reserve(std::min<std::size_t>(262144u, kMaximumTriangles));

    for (const Scene::RenderItem& item : items) {
        if (state.triangles.size() >= kMaximumTriangles) break;
        if (!item.mesh || !item.transform || !item.mesh_component) continue;
        if (item.mesh->indices.size() < 3u || item.mesh->vertices.empty()) continue;
        if (item.material && item.material->opacity < 0.5f) continue;

        const Math::Mat4 model = Math::modelMatrix(*item.transform);
        const Animation::Pose *pose = nullptr;
        const Animation::SkinBindingComponent *binding =
            world.get<Animation::SkinBindingComponent>(item.entity);
        if (binding && binding->animator != Ecs::INVALID_ENTITY) {
            const Animation::AnimatorComponent *animator =
                world.get<Animation::AnimatorComponent>(binding->animator);
            if (animator && !animator->pose.skin.empty()) pose = &animator->pose;
        }

        std::vector<Vec3> positions(item.mesh->vertices.size());
        for (std::size_t index = 0u; index < item.mesh->vertices.size(); ++index) {
            const Models::Vertex& vertex = item.mesh->vertices[index];
            Vec3 local_position{vertex.position.x, vertex.position.y, vertex.position.z};
            if (pose) {
                Animation::Vec3 skinned_position{};
                Animation::Vec3 skinned_normal{};
                Animation::skinVertex(
                    *pose,
                    vertex.skin,
                    {local_position.x, local_position.y, local_position.z},
                    {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                    &skinned_position,
                    &skinned_normal
                );
                local_position = {skinned_position.x, skinned_position.y, skinned_position.z};
            }
            positions[index] = Math::transformPoint(model, local_position);
        }

        const Vec3 base_color = item.material
            ? Vec3{item.material->color.x, item.material->color.y, item.material->color.z}
            : Vec3{1.0f, 1.0f, 1.0f};
        const Models::TextureHandle texture = item.material
            ? item.material->diffuse_texture
            : Models::INVALID_TEXTURE;

        const std::size_t triangle_count = item.mesh->indices.size() / 3u;
        for (std::size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
            if (state.triangles.size() >= kMaximumTriangles) break;
            const std::size_t offset = triangle_index * 3u;
            const std::uint32_t i0 = item.mesh->indices[offset + 0u];
            const std::uint32_t i1 = item.mesh->indices[offset + 1u];
            const std::uint32_t i2 = item.mesh->indices[offset + 2u];
            if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) continue;

            Triangle triangle;
            triangle.p0 = positions[i0];
            triangle.p1 = positions[i1];
            triangle.p2 = positions[i2];
            triangle.uv0 = {item.mesh->vertices[i0].uv.x, item.mesh->vertices[i0].uv.y};
            triangle.uv1 = {item.mesh->vertices[i1].uv.x, item.mesh->vertices[i1].uv.y};
            triangle.uv2 = {item.mesh->vertices[i2].uv.x, item.mesh->vertices[i2].uv.y};
            triangle.normal = normalize(cross(
                subtract(triangle.p1, triangle.p0),
                subtract(triangle.p2, triangle.p0)
            ));
            triangle.base_color = base_color;
            triangle.texture = texture;
            triangle.bounds.include(triangle.p0);
            triangle.bounds.include(triangle.p1);
            triangle.bounds.include(triangle.p2);
            state.scene_bounds.include(triangle.bounds);
            state.triangles.push_back(triangle);
        }
    }

    state.triangle_order.resize(state.triangles.size());
    std::iota(state.triangle_order.begin(), state.triangle_order.end(), 0u);
    if (!state.triangles.empty()) {
        state.nodes.reserve(state.triangles.size() * 2u);
        buildNode(0u, static_cast<std::uint32_t>(state.triangles.size()));
    }
}

void advanceCalculation()
{
    if (!state.calculating || !state.working.valid()) return;

    const std::size_t end = std::min(
        state.probe_cursor + kProbeBudgetPerFrame,
        state.working.probes.size()
    );
    while (state.probe_cursor < end) {
        solveProbe(state.probe_cursor);
        ++state.probe_cursor;
    }

    if (state.probe_cursor < state.working.probes.size()) return;

    state.working.intensity = state.published.intensity;
    state.working.revision = state.next_field_revision++;
    state.published = state.working;

    if (state.bounce_index + 1u >= state.bounces) {
        state.calculating = false;
        return;
    }

    state.source = state.published;
    state.source.intensity = 1.0f;
    state.working.probes.assign(state.working.probes.size(), Probe{});
    state.working.intensity = 1.0f;
    state.probe_cursor = 0u;
    ++state.bounce_index;
}

} // namespace

const Field *update(const Ecs::World& world)
{
    const SettingsState settings = settingsState(world);
    if (!settings.valid) return nullptr;

    const float intensity = std::max(settings.component.intensity, 0.0f);
    const std::uint8_t bounces = std::clamp<std::uint8_t>(settings.component.bounces, 1u, 4u);

    if (state.world != &world) {
        reset();
        state.world = &world;
    }

    const std::uint64_t revision = world.changeRevision();
    if (revision != state.world_revision) {
        Scene::collectRenderItems(world, state.render_items);
        const std::uint64_t geometry_signature = geometrySignature(world, state.render_items);
        const Scene::LightState light = Scene::lightState(world);
        const std::uint64_t light_signature = lightSignature(light);

        const bool geometry_changed = geometry_signature != state.geometry_signature;
        const bool light_changed = light_signature != state.light_signature;
        const bool bounce_changed = bounces != state.bounces;

        state.light = light;
        state.world_revision = revision;

        if (geometry_changed) {
            state.geometry_signature = geometry_signature;
            buildGeometry(world, state.render_items);
        }
        if (light_changed) state.light_signature = light_signature;
        state.bounces = bounces;

        if (geometry_changed || light_changed || bounce_changed || !state.published.valid()) {
            restartCalculation(intensity);
        }
    }

    state.published.intensity = intensity;
    advanceCalculation();
    return state.published.valid() ? &state.published : nullptr;
}

Vec3 sample(const Field *field, Vec3 position, Vec3 normal)
{
    return sampleField(field, position, normal, true);
}

void reset()
{
    state = State{};
}

} // namespace Renderer::GlobalIllumination
