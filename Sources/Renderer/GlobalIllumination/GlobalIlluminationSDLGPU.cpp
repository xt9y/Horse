#include "Renderer/GlobalIllumination/GlobalIlluminationSDLGPU.hpp"

#include "Renderer/Environment.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/Internal/ShadingState.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr std::size_t HeaderVec4Count = 12u;
constexpr std::size_t LightVec4Count = 5u;
constexpr float Pi = 3.14159265358979323846f;

SDL_GPUBuffer *buffer = nullptr;
std::size_t capacity = 0u;
std::uint64_t uploaded_revision = std::numeric_limits<std::uint64_t>::max();
std::uint64_t uploaded_environment = std::numeric_limits<std::uint64_t>::max();
std::uint64_t uploaded_lighting = std::numeric_limits<std::uint64_t>::max();
bool uploaded_valid = false;

bool upload(const GlobalIllumination::Field *field)
{
    if (!Renderer::SDLGPU::device()) return false;
    const bool valid = field && field->valid();
    const std::uint64_t revision = valid ? field->revision : 0u;
    const ShadingState& shading = shadingState();
    const std::uint64_t environment_revision = environmentSignature(shading.environment);
    const std::uint64_t lighting_revision = shading.lighting.revision;
    if (buffer && revision == uploaded_revision && environment_revision == uploaded_environment &&
        lighting_revision == uploaded_lighting && valid == uploaded_valid)
        return true;

    const std::size_t light_count = shading.lighting.lights.size();
    const std::size_t probe_count = valid ? field->probes.size() : 0u;
    const std::size_t light_base = HeaderVec4Count + probe_count * 4u;
    std::vector<float> data((light_base + light_count * LightVec4Count) * 4u, 0.0f);

    if (valid) {
        data[0] = field->minimum.x; data[1] = field->minimum.y; data[2] = field->minimum.z; data[3] = 1.0f;
        data[4] = field->maximum.x; data[5] = field->maximum.y; data[6] = field->maximum.z; data[7] = std::max(field->intensity, 0.0f);
        data[8] = static_cast<float>(field->size_x); data[9] = static_cast<float>(field->size_y);
        data[10] = static_cast<float>(field->size_z); data[11] = static_cast<float>(probe_count);
    }

    const EnvironmentState& environment = shading.environment;
    if (environment.valid) {
        data[12] = environment.average_color.x; data[13] = environment.average_color.y; data[14] = environment.average_color.z; data[15] = std::max(environment.intensity, 0.0f);
        data[16] = environment.sky_color.x; data[17] = environment.sky_color.y; data[18] = environment.sky_color.z;
        data[19] = environment.texture != Models::INVALID_TEXTURE ? 1.0f : 0.0f;
        data[20] = environment.fog_color.x; data[21] = environment.fog_color.y; data[22] = environment.fog_color.z; data[23] = static_cast<float>(environment.fog);
        data[24] = environment.ambient_color.x; data[25] = environment.ambient_color.y; data[26] = environment.ambient_color.z; data[27] = std::max(environment.ambient_intensity, 0.0f);
        data[28] = std::max(environment.fog_density, 0.0f); data[29] = std::max(environment.fog_start, 0.0f);
        data[30] = std::max(environment.fog_end, environment.fog_start + 1.0e-4f); data[31] = environment.rotation_degrees * (Pi / 180.0f);
    }

    data[46] = static_cast<float>(light_count);
    data[47] = static_cast<float>(light_base);

    if (valid) {
        for (std::size_t probe = 0u; probe < probe_count; ++probe) {
            for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
                const Vec3 value = field->probes[probe].sh[coefficient];
                const std::size_t offset = (HeaderVec4Count + probe * 4u + coefficient) * 4u;
                data[offset] = value.x;
                data[offset + 1u] = value.y;
                data[offset + 2u] = value.z;
            }
        }
    }

    for (std::size_t index = 0u; index < light_count; ++index) {
        const Scenes::LightState& light = shading.lighting.lights[index];
        const float inner = std::cos(light.inner_cone_degrees * (Pi / 180.0f));
        const float outer = std::cos(light.outer_cone_degrees * (Pi / 180.0f));
        const float type = light.type == LightType::Point
            ? 1.0f
            : (light.type == LightType::Directional ? 2.0f : 3.0f);
        const std::size_t offset = (light_base + index * LightVec4Count) * 4u;

        data[offset + 0u] = light.position.x;
        data[offset + 1u] = light.position.y;
        data[offset + 2u] = light.position.z;
        data[offset + 3u] = std::max(light.intensity, 0.0f);
        data[offset + 4u] = light.direction.x;
        data[offset + 5u] = light.direction.y;
        data[offset + 6u] = light.direction.z;
        data[offset + 7u] = type;
        data[offset + 8u] = light.color.x;
        data[offset + 9u] = light.color.y;
        data[offset + 10u] = light.color.z;
        data[offset + 11u] = std::max(light.range, 0.0f);
        data[offset + 12u] = inner;
        data[offset + 13u] = outer;
        data[offset + 14u] = light.shadows ? 1.0f : 0.0f;
        data[offset + 15u] = std::max(light.shadow_bias, 0.0f);
        data[offset + 16u] = std::max(light.volumetric_intensity, 0.0f);
        data[offset + 17u] = light.volumetric ? 1.0f : 0.0f;
    }

    const std::size_t bytes = data.size() * sizeof(float);
    if (!buffer || capacity < bytes) {
        SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
            bytes,
            data.data(),
            "Horse Shading State");
        if (!replacement) return false;
        if (buffer) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), buffer);
        buffer = replacement;
        capacity = bytes;
    } else {
        SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
        if (!command || !Renderer::SDLGPU::uploadBuffer(command, buffer, data.data(), bytes, true) ||
            !SDL_SubmitGPUCommandBuffer(command))
        {
            if (command) SDL_CancelGPUCommandBuffer(command);
            return false;
        }
    }

    uploaded_revision = revision;
    uploaded_environment = environment_revision;
    uploaded_lighting = lighting_revision;
    uploaded_valid = valid;
    return true;
}

} // namespace

bool bindGlobalIlluminationSDLGPU(
    SDL_GPURenderPass *pass,
    const GlobalIllumination::Field *field,
    std::uint32_t slot)
{
    if (!pass || !upload(field)) return false;
    SDL_GPUBuffer *buffers[] = {buffer};
    SDL_BindGPUFragmentStorageBuffers(pass, slot, buffers, 1u);
    return true;
}

bool bindGlobalIlluminationSDLGPU(
    SDL_GPUComputePass *pass,
    const GlobalIllumination::Field *field,
    std::uint32_t slot)
{
    if (!pass || !upload(field)) return false;
    SDL_GPUBuffer *buffers[] = {buffer};
    SDL_BindGPUComputeStorageBuffers(pass, slot, buffers, 1u);
    return true;
}

void shutdownGlobalIlluminationSDLGPU()
{
    if (buffer && Renderer::SDLGPU::device())
        SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), buffer);
    buffer = nullptr;
    capacity = 0u;
    uploaded_revision = std::numeric_limits<std::uint64_t>::max();
    uploaded_environment = std::numeric_limits<std::uint64_t>::max();
    uploaded_lighting = std::numeric_limits<std::uint64_t>::max();
    uploaded_valid = false;
}

} // namespace Renderer::Internal
