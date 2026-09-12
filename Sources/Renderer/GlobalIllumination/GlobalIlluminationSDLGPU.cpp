#include "Renderer/GlobalIllumination/GlobalIlluminationSDLGPU.hpp"

#include "Renderer/Environment.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/ShadingState.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace Renderer::Internal {
namespace {

constexpr std::size_t HeaderVec4Count = 12u;
constexpr float Pi = 3.14159265358979323846f;

SDL_GPUBuffer *buffer = nullptr;
std::size_t capacity = 0u;
std::uint64_t uploaded_revision = std::numeric_limits<std::uint64_t>::max();
std::uint64_t uploaded_environment = std::numeric_limits<std::uint64_t>::max();
std::uint64_t uploaded_light = std::numeric_limits<std::uint64_t>::max();
bool uploaded_valid = false;

bool upload(const GlobalIllumination::Field *field)
{
    if (!Renderer::SDLGPU::device()) return false;
    const bool valid = field && field->valid();
    const std::uint64_t revision = valid ? field->revision : 0u;
    const ShadingState& shading = shadingState();
    const std::uint64_t environment_revision = environmentSignature(shading.environment);
    const std::uint64_t light_revision = Renderer::Scenes::lightSignature(shading.light);
    if (buffer && revision == uploaded_revision && environment_revision == uploaded_environment &&
        light_revision == uploaded_light && valid == uploaded_valid)
        return true;

    const std::size_t probe_count = valid ? field->probes.size() : 0u;
    std::vector<float> data((HeaderVec4Count + probe_count * 4u) * 4u, 0.0f);

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

    const Renderer::Scenes::LightState& light = shading.light;
    if (light.valid) {
        const float inner = std::cos(light.inner_cone_degrees * (Pi / 180.0f));
        const float outer = std::cos(light.outer_cone_degrees * (Pi / 180.0f));
        data[32] = light.position.x; data[33] = light.position.y; data[34] = light.position.z; data[35] = std::max(light.intensity, 0.0f);
        data[36] = light.direction.x; data[37] = light.direction.y; data[38] = light.direction.z;
        data[39] = light.type == LightType::Point ? 1.0f : (light.type == LightType::Directional ? 2.0f : 3.0f);
        data[40] = light.color.x; data[41] = light.color.y; data[42] = light.color.z; data[43] = std::max(light.range, 0.0f);
        data[44] = inner; data[45] = outer;
    }

    if (valid) {
        for (std::size_t probe = 0; probe < probe_count; ++probe) {
            for (std::size_t coefficient = 0; coefficient < 4u; ++coefficient) {
                const Vec3 value = field->probes[probe].sh[coefficient];
                const std::size_t offset = (HeaderVec4Count + probe * 4u + coefficient) * 4u;
                data[offset] = value.x;
                data[offset + 1u] = value.y;
                data[offset + 2u] = value.z;
            }
        }
    }

    const std::size_t bytes = data.size() * sizeof(float);
    if (!buffer || capacity < bytes) {
        SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
            bytes,
            data.data(),
            "Horse Global Illumination");
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
    uploaded_light = light_revision;
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
    uploaded_light = std::numeric_limits<std::uint64_t>::max();
    uploaded_valid = false;
}

} // namespace Renderer::Internal
