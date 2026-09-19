#include "Renderer/Internal/ReflectionProbesSDLGPU.hpp"

#include "Models/Internal/TextureStorage.hpp"
#include "Renderer/Internal/ReflectionPrefilter.hpp"
#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Renderer::Reflections::SDLGPU {
namespace {

constexpr std::size_t ProbeVec4Count = 3u;

void hash(std::uint64_t& value, std::uint64_t input)
{
    value ^= input;
    value *= 1099511628211ull;
}

bool textureReady(Models::TextureHandle handle)
{
    if (handle == Models::INVALID_TEXTURE || !Models::Internal::textureStorageReady(handle))
        return false;
    const Models::TextureAsset *asset = Models::texture(handle);
    return asset && asset->image.width > 0 && asset->image.height > 0 &&
        !asset->image.rgba.empty();
}

const Models::Images::Image& textureImage(
    Models::TextureHandle handle,
    const Models::Images::Image& fallback)
{
    if (!textureReady(handle)) return fallback;
    const Models::TextureAsset *asset = Models::texture(handle);
    return asset ? asset->image : fallback;
}

std::uint64_t sourceSignature(
    const State& state,
    const EnvironmentState& environment,
    const ProbeAtlasLayout& layout,
    Quality quality,
    std::uint64_t storage_generation,
    bool environment_ready)
{
    std::uint64_t value = 1469598103934665603ull;
    hash(value, static_cast<std::uint32_t>(quality));
    hash(value, layout.width);
    hash(value, layout.height);
    hash(value, layout.probe_count);
    hash(value, layout.layers);
    hash(value, layout.mip_levels);
    hash(value, storage_generation);
    hash(value, environment_ready ? environment.texture : Models::INVALID_TEXTURE);
    for (std::uint32_t index = 0u; index < layout.probe_count; ++index)
        hash(value, state.probes[index].texture);
    return value;
}

std::uint64_t metadataSignature(
    const State& state,
    const ProbeAtlasLayout& layout,
    Quality quality,
    std::uint64_t storage_generation,
    bool environment_ready)
{
    std::uint64_t value = state.revision;
    hash(value, static_cast<std::uint32_t>(quality));
    hash(value, layout.probe_count);
    hash(value, layout.mip_levels);
    hash(value, storage_generation);
    hash(value, environment_ready ? 1u : 0u);
    return value;
}

std::vector<float> probeMetadata(
    const State& state,
    const ProbeAtlasLayout& layout,
    Quality quality,
    bool environment_ready)
{
    std::vector<float> data((1u + layout.probe_count * ProbeVec4Count) * 4u, 0.0f);
    data[0] = static_cast<float>(layout.probe_count);
    data[1] = static_cast<float>(
        limitEnvironmentMipLevels(layout.mip_levels, quality));
    data[2] = environment_ready ? 1.0f : 0.0f;

    for (std::uint32_t index = 0u; index < layout.probe_count; ++index) {
        const Probe& probe = state.probes[index];
        const std::size_t offset = 4u + static_cast<std::size_t>(index) * ProbeVec4Count * 4u;
        data[offset + 0u] = probe.position.x;
        data[offset + 1u] = probe.position.y;
        data[offset + 2u] = probe.position.z;
        data[offset + 3u] = textureReady(probe.texture)
            ? std::max(probe.intensity, 0.0f)
            : 0.0f;
        data[offset + 4u] = std::max(probe.half_extents.x, 0.0f);
        data[offset + 5u] = std::max(probe.half_extents.y, 0.0f);
        data[offset + 6u] = std::max(probe.half_extents.z, 0.0f);
        data[offset + 7u] = std::max(probe.blend_distance, 0.0f);
        data[offset + 8u] = static_cast<float>(index + 1u);
        data[offset + 9u] = std::bit_cast<float>(static_cast<std::int32_t>(probe.priority));
    }
    return data;
}

bool uploadPrefilteredLayer(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *texture,
    std::uint32_t layer,
    const Models::Images::Image& source,
    const ProbeAtlasLayout& layout,
    Quality quality)
{
    const Internal::PrefilterChain chain = Internal::prefilterEquirectangular(
        source,
        layout.width,
        layout.height,
        layout.mip_levels,
        quality);
    if (chain.levels.size() != layout.mip_levels) return false;

    for (std::uint32_t mip = 0u; mip < layout.mip_levels; ++mip) {
        const Internal::PrefilterLevel& level = chain.levels[mip];
        if (!Renderer::SDLGPU::uploadTextureRgba8Subresource(
                command,
                texture,
                layer,
                mip,
                level.width,
                level.height,
                level.rgba.data(),
                level.rgba.size()))
            return false;
    }
    return true;
}

} // namespace

ProbeAtlas::~ProbeAtlas()
{
    clear();
}

bool ProbeAtlas::sync(
    const State& state,
    const EnvironmentState& environment,
    std::string *error)
{
    if (error) error->clear();
    if (!Renderer::SDLGPU::device()) {
        if (error) *error = "SDL_GPU device is not initialized";
        return false;
    }

    const Quality current_quality = Reflections::quality();
    const bool environment_ready =
        environment.valid && textureReady(environment.texture);
    const ProbeAtlasLayout layout = probeAtlasLayout(
        static_cast<std::uint32_t>(std::min<std::size_t>(state.probes.size(), UINT32_MAX)),
        environment_ready,
        current_quality);
    const std::uint64_t storage_generation = Models::Internal::textureStorageGeneration();
    const std::uint64_t texture_signature = sourceSignature(
        state,
        environment,
        layout,
        current_quality,
        storage_generation,
        environment_ready);
    const std::uint64_t metadata_signature = metadataSignature(
        state,
        layout,
        current_quality,
        storage_generation,
        environment_ready);

    if (!sampler_) sampler_ = Renderer::SDLGPU::createLinearSampler();
    if (!sampler_) {
        if (error) *error = "failed to create reflection probe sampler";
        return false;
    }

    if (!texture_ || texture_signature != source_signature_ ||
        storage_generation != texture_storage_generation_ || current_quality != quality_)
    {
        SDL_GPUTexture *replacement = Renderer::SDLGPU::createTextureArray(
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            SDL_GPU_TEXTUREUSAGE_SAMPLER,
            layout.width,
            layout.height,
            layout.layers,
            layout.mip_levels,
            "Horse Reflection Atlas");
        if (!replacement) {
            if (error) *error = "failed to create reflection texture atlas";
            return false;
        }

        SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
        if (!command) {
            SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), replacement);
            if (error) *error = "failed to acquire reflection atlas upload command buffer";
            return false;
        }

        const Models::Images::Image empty;
        bool uploaded = uploadPrefilteredLayer(
            command,
            replacement,
            0u,
            textureImage(environment_ready ? environment.texture : Models::INVALID_TEXTURE, empty),
            layout,
            current_quality);

        for (std::uint32_t index = 0u; uploaded && index < layout.probe_count; ++index) {
            uploaded = uploadPrefilteredLayer(
                command,
                replacement,
                index + 1u,
                textureImage(state.probes[index].texture, empty),
                layout,
                current_quality);
        }

        if (!uploaded || !SDL_SubmitGPUCommandBuffer(command)) {
            SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), replacement);
            if (error) *error = "failed to upload prefiltered reflection atlas";
            return false;
        }

        if (texture_) SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), texture_);
        texture_ = replacement;
        source_signature_ = texture_signature;
        texture_storage_generation_ = storage_generation;
        quality_ = current_quality;
    }

    if (!buffer_ || metadata_signature != metadata_signature_) {
        const std::vector<float> data = probeMetadata(
            state,
            layout,
            current_quality,
            environment_ready);
        const std::size_t bytes = data.size() * sizeof(float);
        if (!buffer_ || buffer_capacity_ < bytes) {
            SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
                SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                bytes,
                data.data(),
                "Horse Reflection Probe State");
            if (!replacement) {
                if (error) *error = "failed to create reflection probe state buffer";
                return false;
            }
            if (buffer_) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), buffer_);
            buffer_ = replacement;
            buffer_capacity_ = bytes;
        } else {
            SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
            if (!command || !Renderer::SDLGPU::uploadBuffer(
                    command,
                    buffer_,
                    data.data(),
                    bytes,
                    true) ||
                !SDL_SubmitGPUCommandBuffer(command))
            {
                if (command) SDL_CancelGPUCommandBuffer(command);
                if (error) *error = "failed to upload reflection probe state buffer";
                return false;
            }
        }
        metadata_signature_ = metadata_signature;
    }

    probe_count_ = layout.probe_count;
    mip_levels_ = layout.mip_levels;
    return true;
}

void ProbeAtlas::bind(
    SDL_GPURenderPass *pass,
    std::uint32_t sampler_slot,
    std::uint32_t buffer_slot) const
{
    if (!pass || !texture_ || !sampler_ || !buffer_) return;
    const SDL_GPUTextureSamplerBinding sampler_binding{texture_, sampler_};
    SDL_BindGPUFragmentSamplers(pass, sampler_slot, &sampler_binding, 1u);
    SDL_GPUBuffer *buffers[] = {buffer_};
    SDL_BindGPUFragmentStorageBuffers(pass, buffer_slot, buffers, 1u);
}

void ProbeAtlas::clear()
{
    SDL_GPUDevice *gpu = Renderer::SDLGPU::device();
    if (gpu) {
        if (buffer_) SDL_ReleaseGPUBuffer(gpu, buffer_);
        if (texture_) SDL_ReleaseGPUTexture(gpu, texture_);
        if (sampler_) SDL_ReleaseGPUSampler(gpu, sampler_);
    }
    buffer_ = nullptr;
    buffer_capacity_ = 0u;
    texture_ = nullptr;
    sampler_ = nullptr;
    source_signature_ = UINT64_MAX;
    metadata_signature_ = UINT64_MAX;
    texture_storage_generation_ = UINT64_MAX;
    quality_ = Quality::High;
    probe_count_ = 0u;
    mip_levels_ = 1u;
}

} // namespace Renderer::Reflections::SDLGPU
