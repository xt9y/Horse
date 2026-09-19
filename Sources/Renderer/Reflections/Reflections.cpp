#include "Renderer/Reflections/Reflections.hpp"

#include "Renderer/Hierarchy.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Renderer::Reflections {
namespace {

Settings& storage()
{
    static Settings value;
    return value;
}

std::uint32_t maximumMipLevels(Quality quality)
{
    switch (quality) {
        case Quality::Low: return 5u;
        case Quality::Medium: return 7u;
        case Quality::High: return 9u;
        case Quality::Ultra: return UINT32_MAX;
    }
    return 9u;
}

void hash(std::uint64_t& value, std::uint64_t input)
{
    value ^= input;
    value *= 1099511628211ull;
}

void hashFloat(std::uint64_t& value, float input)
{
    hash(value, std::bit_cast<std::uint32_t>(input));
}

void hashVec3(std::uint64_t& value, Vec3 input)
{
    hashFloat(value, input.x);
    hashFloat(value, input.y);
    hashFloat(value, input.z);
}

float axisInfluence(float delta, float extent, float blend_distance)
{
    extent = std::max(extent, 0.0f);
    const float edge_distance = extent - std::abs(delta);
    if (edge_distance <= 0.0f) return 0.0f;
    if (blend_distance <= 0.0f) return 1.0f;
    return std::clamp(edge_distance / blend_distance, 0.0f, 1.0f);
}

} // namespace

Settings& settings()
{
    return storage();
}

const Settings& currentSettings()
{
    return storage();
}

void setQuality(Quality value)
{
    storage().quality = value;
}

Quality quality()
{
    return storage().quality;
}

std::uint32_t maximumProbes(Quality quality)
{
    switch (quality) {
        case Quality::Low: return 2u;
        case Quality::Medium: return 4u;
        case Quality::High: return 8u;
        case Quality::Ultra: return 16u;
    }
    return 8u;
}

std::uint32_t probeResolution(Quality quality)
{
    switch (quality) {
        case Quality::Low: return 256u;
        case Quality::Medium: return 512u;
        case Quality::High:
        case Quality::Ultra: return 1024u;
    }
    return 1024u;
}

ProbeAtlasLayout probeAtlasLayout(
    std::uint32_t probe_count,
    bool environment_texture,
    Quality quality)
{
    ProbeAtlasLayout layout;
    layout.probe_count = std::min(probe_count, maximumProbes(quality));
    if (layout.probe_count == 0u && !environment_texture) return layout;

    layout.width = std::max(probeResolution(quality), 1u);
    layout.height = std::max(layout.width / 2u, 1u);
    layout.layers = 1u + layout.probe_count;
    layout.mip_levels = environmentMipLevels(layout.width, layout.height, quality);
    return layout;
}

State state(const Ecs::World& world)
{
    State result;

    world.each<ProbeComponent, Transform>(
        [&](Ecs::Entity entity, const ProbeComponent& component, const Transform& local) {
            if (!component.enabled || component.texture == Models::INVALID_TEXTURE) return;

            Transform transform = local;
            (void)Hierarchy::worldTransform(world, entity, &transform);

            Probe probe;
            probe.entity = entity;
            probe.texture = component.texture;
            probe.position = transform.position;
            probe.half_extents = {
                std::max(component.half_extents.x, 0.0f) * std::abs(transform.scale.x),
                std::max(component.half_extents.y, 0.0f) * std::abs(transform.scale.y),
                std::max(component.half_extents.z, 0.0f) * std::abs(transform.scale.z),
            };
            probe.blend_distance = std::max(component.blend_distance, 0.0f);
            probe.intensity = std::max(component.intensity, 0.0f);
            probe.priority = component.priority;
            result.probes.push_back(probe);
        }
    );

    std::sort(
        result.probes.begin(),
        result.probes.end(),
        [](const Probe& a, const Probe& b) {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.entity < b.entity;
        }
    );

    std::uint64_t revision = 1469598103934665603ull;
    hash(revision, result.probes.size());
    for (const Probe& probe : result.probes) {
        hash(revision, probe.entity);
        hash(revision, probe.texture);
        hashVec3(revision, probe.position);
        hashVec3(revision, probe.half_extents);
        hashFloat(revision, probe.blend_distance);
        hashFloat(revision, probe.intensity);
        hash(revision, static_cast<std::uint32_t>(probe.priority));
    }
    result.revision = revision;
    return result;
}

float probeInfluence(const Probe& probe, Vec3 position)
{
    const Vec3 delta {
        position.x - probe.position.x,
        position.y - probe.position.y,
        position.z - probe.position.z,
    };
    return std::min({
        axisInfluence(delta.x, probe.half_extents.x, probe.blend_distance),
        axisInfluence(delta.y, probe.half_extents.y, probe.blend_distance),
        axisInfluence(delta.z, probe.half_extents.z, probe.blend_distance),
    });
}

Blend blend(const State& state, Vec3 position)
{
    Blend result;
    int priority = 0;
    bool priority_set = false;

    for (std::uint32_t index = 0u; index < state.probes.size(); ++index) {
        const Probe& probe = state.probes[index];
        const float influence = probeInfluence(probe, position);
        if (influence <= 0.0f) continue;

        if (!priority_set) {
            priority = probe.priority;
            priority_set = true;
        } else if (probe.priority != priority) {
            break;
        }

        if (result.count < 2u) {
            result.indices[result.count] = index;
            result.weights[result.count] = influence;
            ++result.count;
            continue;
        }

        const std::uint32_t weaker = result.weights[0] <= result.weights[1] ? 0u : 1u;
        if (influence > result.weights[weaker]) {
            result.indices[weaker] = index;
            result.weights[weaker] = influence;
        }
    }

    if (result.count == 0u) return result;
    if (result.count == 1u) {
        result.weights[0] = 1.0f;
        return result;
    }

    if (result.weights[1] > result.weights[0]) {
        std::swap(result.indices[0], result.indices[1]);
        std::swap(result.weights[0], result.weights[1]);
    }

    const float total = result.weights[0] + result.weights[1];
    if (total <= 1.0e-8f) {
        result.count = 0u;
        result.indices = {Blend::InvalidIndex, Blend::InvalidIndex};
        result.weights = {0.0f, 0.0f};
        return result;
    }
    result.weights[0] /= total;
    result.weights[1] /= total;
    return result;
}

std::uint32_t limitEnvironmentMipLevels(
    std::uint32_t available_levels,
    Quality quality)
{
    return std::min(std::max(available_levels, 1u), maximumMipLevels(quality));
}

std::uint32_t environmentMipLevels(
    std::uint32_t width,
    std::uint32_t height,
    Quality quality)
{
    std::uint32_t size = std::max(width, height);
    if (size == 0u) return 1u;

    std::uint32_t levels = 1u;
    while (size > 1u) {
        size >>= 1u;
        ++levels;
    }
    return limitEnvironmentMipLevels(levels, quality);
}

float environmentLod(float roughness, std::uint32_t mip_levels)
{
    if (mip_levels <= 1u) return 0.0f;
    return std::clamp(roughness, 0.0f, 1.0f) *
        static_cast<float>(mip_levels - 1u);
}

} // namespace Renderer::Reflections
