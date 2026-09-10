#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"

#include "Renderer/GlobalIllumination/PhotonMapping/PhotonMap.hpp"
#include "Renderer/GlobalIllumination/TraceScene.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Renderer::GlobalIllumination {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kY00 = 0.28209479177387814f;
constexpr float kY1 = 0.48860251190291992f;
constexpr float kRayEpsilon = 0.0025f;
constexpr std::size_t kRaysPerProbe = 48u;
constexpr std::size_t kProbeBudgetPerFrame = 16u;

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

Vec3 multiply(Vec3 a, Vec3 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 divide(Vec3 value, float scalar)
{
    return scalar == 0.0f ? Vec3{} : multiply(value, 1.0f / scalar);
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float lengthSquared(Vec3 value)
{
    return dot(value, value);
}

Vec3 normalize(Vec3 value)
{
    const float squared = lengthSquared(value);
    if (squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    return divide(value, std::sqrt(squared));
}

Vec3 clampPositive(Vec3 value)
{
    return {
        std::max(value.x, 0.0f),
        std::max(value.y, 0.0f),
        std::max(value.z, 0.0f),
    };
}

struct SettingsState {
    bool valid = false;
    GlobalIlluminationComponent component{};
};

struct State {
    const Ecs::World *world = nullptr;
    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t geometry_signature = 0u;
    std::uint64_t light_signature = 0u;
    std::uint64_t next_field_revision = 1u;

    std::uint8_t bounces = 2u;
    PhotonMapping::Settings photon_settings{};
    Scenes::LightState light{};
    std::vector<Scenes::Scene::RenderItem> render_items;
    TraceScene trace_scene;
    PhotonMapping::PhotonMap photon_map;

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
        const GlobalIlluminationComponent *component = world.get<GlobalIlluminationComponent>(entity);
        if (!component) continue;
        result.valid = component->enabled;
        result.component = *component;
        break;
    }
    return result;
}

bool photonSettingsEqual(
    const PhotonMapping::Settings& a,
    const PhotonMapping::Settings& b)
{
    return a.enabled == b.enabled &&
        a.photon_count == b.photon_count &&
        a.bounces == b.bounces &&
        a.radius == b.radius;
}

Vec3 directIrradiance(const TraceHit& hit)
{
    if (!state.light.valid || state.light.intensity <= 0.0f) return {};

    Vec3 light_direction{};
    float attenuation = 1.0f;
    float shadow_distance = std::numeric_limits<float>::infinity();

    if (state.light.type == LightType::Directional) {
        light_direction = multiply(normalize(state.light.direction), -1.0f);
    } else {
        const Vec3 to_light = subtract(state.light.position, hit.position);
        const float distance_squared = std::max(lengthSquared(to_light), 1.0e-4f);
        const float distance = std::sqrt(distance_squared);
        light_direction = divide(to_light, distance);
        attenuation = 1.0f / distance_squared;
        shadow_distance = std::max(distance - kRayEpsilon * 8.0f, 0.0f);
    }

    const float cosine = std::max(dot(hit.normal, light_direction), 0.0f);
    if (cosine <= 0.0f) return {};

    const Vec3 shadow_origin = add(hit.position, multiply(hit.normal, kRayEpsilon * 4.0f));
    if (state.trace_scene.occluded(shadow_origin, light_direction, shadow_distance)) return {};

    return multiply(
        clampPositive(state.light.color),
        state.light.intensity * attenuation * cosine
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
    const float phase = golden_angle *
        (static_cast<float>(ray) + static_cast<float>(probe) * 0.61803398875f);
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
        const TraceHit hit = state.trace_scene.traceClosest(
            origin,
            direction,
            std::numeric_limits<float>::infinity()
        );
        if (!hit.found) continue;

        Vec3 irradiance = directIrradiance(hit);
        if (state.photon_map.valid()) {
            irradiance = add(irradiance, state.photon_map.sample(hit.position, hit.normal));
        } else if (state.bounce_index > 0u && state.source.valid()) {
            irradiance = add(irradiance, sampleField(&state.source, hit.position, hit.normal, false));
        }

        const Vec3 outgoing = multiply(
            multiply(state.trace_scene.albedo(hit), irradiance),
            1.0f / kPi
        );
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

void restartCalculation(float intensity)
{
    const TraceBounds scene_bounds = state.trace_scene.bounds();
    if (!scene_bounds.valid || state.trace_scene.empty()) {
        state.published = {};
        state.working = {};
        state.source = {};
        state.calculating = false;
        return;
    }

    const Vec3 raw_extent = subtract(scene_bounds.maximum, scene_bounds.minimum);
    const float maximum_extent = std::max({raw_extent.x, raw_extent.y, raw_extent.z, 1.0e-3f});
    const float margin = std::max(maximum_extent * 0.05f, 0.25f);

    Field field;
    field.minimum = subtract(scene_bounds.minimum, Vec3{margin, margin, margin});
    field.maximum = add(scene_bounds.maximum, Vec3{margin, margin, margin});
    const Vec3 extent = subtract(field.maximum, field.minimum);
    const float field_maximum = std::max({extent.x, extent.y, extent.z});
    field.size_x = dimensionFor(extent.x, field_maximum);
    field.size_y = dimensionFor(extent.y, field_maximum);
    field.size_z = dimensionFor(extent.z, field_maximum);
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

    if (state.photon_map.valid() || state.bounce_index + 1u >= state.bounces) {
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
    PhotonMapping::Settings photon_settings;
    photon_settings.enabled = settings.component.photon_mapping;
    photon_settings.photon_count = std::min(settings.component.photon_count, 262144u);
    photon_settings.bounces = bounces;
    photon_settings.radius = std::max(settings.component.photon_radius, 0.0f);

    if (state.world != &world) {
        reset();
        state.world = &world;
    }

    bool geometry_changed = false;
    bool light_changed = false;
    const std::uint64_t revision = world.changeRevision();
    if (revision != state.world_revision) {
        Scenes::Scene::collectRenderItems(world, state.render_items);
        const std::uint64_t geometry_signature = state.trace_scene.signature(world, state.render_items);
        const Scenes::LightState light = Scenes::lightState(Scenes::Scene::lightState(world));
        const std::uint64_t light_signature = Scenes::lightSignature(light);

        geometry_changed = geometry_signature != state.geometry_signature;
        light_changed = light_signature != state.light_signature;
        state.world_revision = revision;
        state.light = light;

        if (geometry_changed) {
            std::string error;
            if (!state.trace_scene.build(world, state.render_items, &error)) {
                std::fprintf(stderr, "[GlobalIllumination]: scene build failed: %s\n", error.c_str());
                state.trace_scene.clear();
            }
            state.geometry_signature = geometry_signature;
        }
        if (light_changed) state.light_signature = light_signature;
    }

    const bool bounce_changed = bounces != state.bounces;
    const bool photon_changed = !photonSettingsEqual(photon_settings, state.photon_settings);
    state.bounces = bounces;

    if (geometry_changed || light_changed || bounce_changed || photon_changed) {
        state.photon_settings = photon_settings;
        if (photon_settings.enabled) {
            state.photon_map.rebuild(state.trace_scene, state.light, photon_settings);
        } else {
            state.photon_map.clear();
        }
    }

    if (geometry_changed || light_changed || bounce_changed || photon_changed || !state.published.valid()) {
        restartCalculation(intensity);
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
