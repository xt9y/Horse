#ifdef __APPLE__

#include "Renderer/GlobalIllumination/Metal/GlobalIlluminationMetal.hpp"

#include "Renderer/ShadingState.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr std::size_t kHeaderVec4Count = 12u;
constexpr float kPi = 3.14159265358979323846f;

LWMGLBuffer buffer = nullptr;
std::size_t capacity = 0u;
std::uint64_t uploaded_revision = std::numeric_limits<std::uint64_t>::max();
std::uint64_t uploaded_environment = std::numeric_limits<std::uint64_t>::max();
std::uint64_t uploaded_light = std::numeric_limits<std::uint64_t>::max();
bool uploaded_valid = false;

bool upload(const GlobalIllumination::Field *field)
{
    const bool valid = field && field->valid();
    const std::uint64_t revision = valid ? field->revision : 0u;
    const ShadingState& shading = shadingState();
    const std::uint64_t environment_revision = environmentSignature(shading.environment);
    const std::uint64_t light_revision = Renderer::Scenes::lightSignature(shading.light);
    if (buffer && revision == uploaded_revision && environment_revision == uploaded_environment &&
        light_revision == uploaded_light && valid == uploaded_valid)
    {
        return true;
    }

    const std::size_t probe_count = valid ? field->probes.size() : 0u;
    std::vector<float> data((kHeaderVec4Count + probe_count * 4u) * 4u, 0.0f);

    if (valid) {
        data[0u] = field->minimum.x; data[1u] = field->minimum.y; data[2u] = field->minimum.z; data[3u] = 1.0f;
        data[4u] = field->maximum.x; data[5u] = field->maximum.y; data[6u] = field->maximum.z; data[7u] = std::max(field->intensity, 0.0f);
        data[8u] = static_cast<float>(field->size_x); data[9u] = static_cast<float>(field->size_y);
        data[10u] = static_cast<float>(field->size_z); data[11u] = static_cast<float>(probe_count);
    }

    const EnvironmentState& environment = shading.environment;
    if (environment.valid) {
        data[12u] = environment.average_color.x; data[13u] = environment.average_color.y; data[14u] = environment.average_color.z; data[15u] = std::max(environment.intensity, 0.0f);
        data[16u] = environment.sky_color.x; data[17u] = environment.sky_color.y; data[18u] = environment.sky_color.z;
        data[19u] = environment.texture != Models::INVALID_TEXTURE ? 1.0f : 0.0f;
        data[20u] = environment.fog_color.x; data[21u] = environment.fog_color.y; data[22u] = environment.fog_color.z; data[23u] = static_cast<float>(environment.fog);
        data[24u] = environment.ambient_color.x; data[25u] = environment.ambient_color.y; data[26u] = environment.ambient_color.z; data[27u] = std::max(environment.ambient_intensity, 0.0f);
        data[28u] = std::max(environment.fog_density, 0.0f); data[29u] = std::max(environment.fog_start, 0.0f);
        data[30u] = std::max(environment.fog_end, environment.fog_start + 1.0e-4f); data[31u] = environment.rotation_degrees * (kPi / 180.0f);
    }

    const Renderer::Scenes::LightState& light = shading.light;
    if (light.valid) {
        const float inner = std::cos(light.inner_cone_degrees * (kPi / 180.0f));
        const float outer = std::cos(light.outer_cone_degrees * (kPi / 180.0f));
        data[32u] = light.position.x; data[33u] = light.position.y; data[34u] = light.position.z; data[35u] = std::max(light.intensity, 0.0f);
        data[36u] = light.direction.x; data[37u] = light.direction.y; data[38u] = light.direction.z;
        data[39u] = light.type == LightType::Point ? 1.0f : (light.type == LightType::Directional ? 2.0f : 3.0f);
        data[40u] = light.color.x; data[41u] = light.color.y; data[42u] = light.color.z; data[43u] = std::max(light.range, 0.0f);
        data[44u] = inner; data[45u] = outer;
    }

    if (valid) {
        for (std::size_t probe = 0u; probe < probe_count; ++probe) {
            for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
                const Vec3 value = field->probes[probe].sh[coefficient];
                const std::size_t offset = (kHeaderVec4Count + probe * 4u + coefficient) * 4u;
                data[offset + 0u] = value.x; data[offset + 1u] = value.y; data[offset + 2u] = value.z;
            }
        }
    }

    const std::size_t bytes = data.size() * sizeof(float);
    if (!buffer || capacity < bytes) {
        if (buffer) Metal.destroyBuffer(buffer);
        const LWMGLBufferDesc desc = {bytes, LWMGL_STORAGE_SHARED};
        buffer = Metal.createBuffer(&desc, data.data());
        if (!buffer) { capacity = 0u; return false; }
        capacity = bytes;
    } else if (Metal.uploadBuffer(buffer, 0u, data.data(), bytes) != 0) {
        return false;
    }

    uploaded_revision = revision;
    uploaded_environment = environment_revision;
    uploaded_light = light_revision;
    uploaded_valid = valid;
    return true;
}

} // namespace

bool bindGlobalIlluminationMetal(
    LWMGLCommand command,
    const GlobalIllumination::Field *field,
    std::uint32_t buffer_index)
{
    if (!command || !Metal.isCreated()) return false;
    if (!upload(field)) return false;
    return Metal.setBuffer(command, buffer, 0u, buffer_index) == 0;
}

void shutdownGlobalIlluminationMetal()
{
    if (buffer) Metal.destroyBuffer(buffer);
    buffer = nullptr;
    capacity = 0u;
    uploaded_revision = std::numeric_limits<std::uint64_t>::max();
    uploaded_environment = std::numeric_limits<std::uint64_t>::max();
    uploaded_light = std::numeric_limits<std::uint64_t>::max();
    uploaded_valid = false;
}

} // namespace Renderer::Internal

#endif
