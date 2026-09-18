#include "Renderer/Lighting/Lighting.hpp"

#include "Renderer/Features.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <algorithm>
#include <bit>

namespace Renderer::Lighting {
namespace {

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

} // namespace

State state(const Ecs::World& world)
{
    State result;
    const Features::Settings& features = Features::currentSettings();
    if (!features.lighting) {
        result.revision = signature(result);
        return result;
    }

    std::vector<Scenes::Scene::LightState> source;
    Scenes::Scene::collectLights(world, source);
    result.lights.reserve(source.size());

    for (const Scenes::Scene::LightState& value : source) {
        Scenes::LightState light = Scenes::lightState(value);
        if (!light.valid) continue;
        light.entity = value.entity;
        light.shadows = features.shadows && value.has_shadow && value.shadow.enabled;
        light.shadow_bias = std::max(value.shadow.bias, 0.0f);
        light.volumetric = value.has_volumetric && value.volumetric.enabled;
        light.volumetric_intensity = light.volumetric
            ? std::max(value.volumetric.intensity, 0.0f)
            : 0.0f;
        result.lights.push_back(light);
    }

    result.revision = signature(result);
    return result;
}

std::uint64_t signature(const State& state)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, state.lights.size());
    for (const Scenes::LightState& light : state.lights) {
        hashValue(hash, light.entity);
        hashValue(hash, Scenes::lightSignature(light));
        hashValue(hash, light.shadows ? 1u : 0u);
        hashFloat(hash, light.shadow_bias);
        hashValue(hash, light.volumetric ? 1u : 0u);
        hashFloat(hash, light.volumetric_intensity);
    }
    return hash;
}

} // namespace Renderer::Lighting
