#include "Renderer/Environment.hpp"

#include "Models/Models.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace Renderer {
namespace {

struct AverageCache {
    std::uint64_t revision = 0u;
    std::unordered_map<Models::TextureHandle, Vec3> colors;
};

AverageCache& averages()
{
    static AverageCache value;
    return value;
}

Vec3 textureAverage(Models::TextureHandle handle, Vec3 fallback)
{
    if (handle == Models::INVALID_TEXTURE) return fallback;

    AverageCache& cache = averages();
    const std::uint64_t revision = Models::resourceRevision();
    if (cache.revision != revision) {
        cache.colors.clear();
        cache.revision = revision;
    }
    if (const auto found = cache.colors.find(handle); found != cache.colors.end())
        return found->second;

    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset || asset->image.rgba.size() < 4u) return fallback;

    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    std::size_t count = 0u;
    const std::size_t pixel_count = asset->image.rgba.size() / 4u;
    const std::size_t step = std::max<std::size_t>(pixel_count / 4096u, 1u);
    for (std::size_t pixel = 0u; pixel < pixel_count; pixel += step) {
        const std::size_t offset = pixel * 4u;
        red += static_cast<double>(asset->image.rgba[offset + 0u]) / 255.0;
        green += static_cast<double>(asset->image.rgba[offset + 1u]) / 255.0;
        blue += static_cast<double>(asset->image.rgba[offset + 2u]) / 255.0;
        ++count;
    }
    if (count == 0u) return fallback;
    const float inverse = 1.0f / static_cast<float>(count);
    const Vec3 result {
        static_cast<float>(red) * inverse,
        static_cast<float>(green) * inverse,
        static_cast<float>(blue) * inverse,
    };
    cache.colors.emplace(handle, result);
    return result;
}

void hash(std::uint64_t& value, std::uint32_t data)
{
    value ^= static_cast<std::uint64_t>(data);
    value *= 1099511628211ull;
}

void hashFloat(std::uint64_t& value, float data)
{
    hash(value, std::bit_cast<std::uint32_t>(data));
}

void hashVec3(std::uint64_t& value, Vec3 data)
{
    hashFloat(value, data.x);
    hashFloat(value, data.y);
    hashFloat(value, data.z);
}

} // namespace

EnvironmentState environmentState(const Ecs::World& world)
{
    EnvironmentState result;
    for (const Ecs::Entity entity : world.entities()) {
        const EnvironmentComponent *component = world.get<EnvironmentComponent>(entity);
        if (!component || !component->enabled) continue;

        result.valid = true;
        result.texture = component->texture;
        result.sky_color = component->sky_color;
        result.intensity = std::max(component->intensity, 0.0f);
        result.rotation_degrees = component->rotation_degrees;
        result.ambient_color = component->ambient_color;
        result.ambient_intensity = std::max(component->ambient_intensity, 0.0f);
        result.fog = component->fog;
        result.fog_color = component->fog_color;
        result.fog_density = std::max(component->fog_density, 0.0f);
        result.fog_start = std::max(component->fog_start, 0.0f);
        result.fog_end = std::max(component->fog_end, result.fog_start + 1.0e-4f);
        result.average_color = textureAverage(component->texture, component->sky_color);
        break;
    }
    return result;
}

std::uint64_t environmentSignature(const EnvironmentState& environment)
{
    std::uint64_t value = 1469598103934665603ull;
    hash(value, environment.valid ? 1u : 0u);
    hash(value, environment.texture);
    hashVec3(value, environment.sky_color);
    hashVec3(value, environment.average_color);
    hashFloat(value, environment.intensity);
    hashFloat(value, environment.rotation_degrees);
    hashVec3(value, environment.ambient_color);
    hashFloat(value, environment.ambient_intensity);
    hash(value, static_cast<std::uint32_t>(environment.fog));
    hashVec3(value, environment.fog_color);
    hashFloat(value, environment.fog_density);
    hashFloat(value, environment.fog_start);
    hashFloat(value, environment.fog_end);
    return value;
}

} // namespace Renderer
