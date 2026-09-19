#include "Renderer/Internal/ReflectionProbesSDLGPU.hpp"

#include "Models/Internal/TextureStorage.hpp"
#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
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

std::uint64_t sourceSignature(
    const State& state,
    const ProbeAtlasLayout& layout,
    Quality quality,
    std::uint64_t storage_generation)
{
    std::uint64_t value = 1469598103934665603ull;
    hash(value, static_cast<std::uint32_t>(quality));
    hash(value, layout.width);
    hash(value, layout.height);
    hash(value, layout.probe_count);
    hash(value, layout.layers);
    hash(value, layout.mip_levels);
    hash(value, storage_generation);
    for (std::uint32_t index = 0u; index < layout.probe_count; ++index)
        hash(value, state.probes[index].texture);
    return value;
}

std::uint64_t metadataSignature(
    const State& state,
    const ProbeAtlasLayout& layout,
    Quality quality,
    std::uint64_t storage_generation)
{
    std::uint64_t value = state.revision;
    hash(value, static_cast<std::uint32_t>(quality));
    hash(value, layout.probe_count);
    hash(value, layout.mip_levels);
    hash(value, storage_generation);
    return value;
}

float srgbToLinear(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

std::array<float, 4> pixel(const Models::Images::Image& image, int x, int y)
{
    if (image.width <= 0 || image.height <= 0 || image.rgba.empty()) return {};

    x %= image.width;
    if (x < 0) x += image.width;
    y = std::clamp(y, 0, image.height - 1);
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(x)) * 4u;
    if (offset + 3u >= image.rgba.size()) return {};

    return {
        srgbToLinear(static_cast<float>(image.rgba[offset + 0u]) / 255.0f),
        srgbToLinear(static_cast<float>(image.rgba[offset + 1u]) / 255.0f),
        srgbToLinear(static_cast<float>(image.rgba[offset + 2u]) / 255.0f),
        static_cast<float>(image.rgba[offset + 3u]) / 255.0f,
    };
}

std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(
        std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f));
}

std::vector<std::uint8_t> resampleEquirect(
    const Models::Images::Image& source,
    std::uint32_t width,
    std::uint32_t height)
{
    std::vector<std::uint8_t> result(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u,
        0u);
    if (source.width <= 0 || source.height <= 0 || source.rgba.empty()) {
        for (std::size_t index = 3u; index < result.size(); index += 4u) result[index] = 255u;
        return result;
    }

    for (std::uint32_t y = 0u; y < height; ++y) {
        const float source_y =
            (static_cast<float>(y) + 0.5f) * static_cast<float>(source.height) /
                static_cast<float>(height) - 0.5f;
        const int y0 = static_cast<int>(std::floor(source_y));
        const int y1 = y0 + 1;
        const float ty = source_y - std::floor(source_y);

        for (std::uint32_t x = 0u; x < width; ++x) {
            const float source_x =
                (static_cast<float>(x) + 0.5f) * static_cast<float>(source.width) /
                    static_cast<float>(width) - 0.5f;
            const int x0 = static_cast<int>(std::floor(source_x));
            const int x1 = x0 + 1;
            const float tx = source_x - std::floor(source_x);

            const auto p00 = pixel(source, x0, y0);
            const auto p10 = pixel(source, x1, y0);
            const auto p01 = pixel(source, x0, y1);
            const auto p11 = pixel(source, x1, y1);
            const std::size_t destination =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;

            for (std::size_t channel = 0u; channel < 4u; ++channel) {
                const float a = p00[channel] + (p10[channel] - p00[channel]) * tx;
                const float b = p01[channel] + (p11[channel] - p01[channel]) * tx;
                float value = a + (b - a) * ty;
                if (channel < 3u) value = linearToSrgb(value);
                result[destination + channel] = toByte(value);
            }
        }
    }
    return result;
}

std::vector<float> probeMetadata(
    const State& state,
    const ProbeAtlasLayout& layout,
    Quality quality)
{
    std::vector<float> data((1u + layout.probe_count * ProbeVec4Count) * 4u, 0.0f);
    data[0] = static_cast<float>(layout.probe_count);
    data[1] = static_cast<float>(
        limitEnvironmentMipLevels(layout.mip_levels, quality));

    for (std::uint32_t index = 0u; index < layout.probe_count; ++index) {
        const Probe& probe = state.probes[index];
        const std::size_t offset = 4u + static_cast<std::size_t>(index) * ProbeVec4Count * 4u;
        data[offset + 0u] = probe.position.x;
        data[offset + 1u] = probe.position.y;
        data[offset + 2u] = probe.position.z;
        data[offset + 3u] = Models::Internal::textureStorageReady(probe.texture)
            ? std::max(probe.intensity, 0.0f)
            : 0.0f;
        data[offset + 4u] = std::max(probe.half_extents.x, 0.0f);
        data[offset + 5u] = std::max(probe.half_extents.y, 0.0f);
        data[offset + 6u] = std::max(probe.half_extents.z, 0.0f);
        data[offset + 7u] = std::max(probe.blend_distance, 0.0f);
        data[offset + 8u] = static_cast<float>(index);
        data[offset + 9u] = static_cast<float>(probe.priority);
    }
    return data;
}

} // namespace

ProbeAtlas::~ProbeAtlas()
{
    clear();
}

bool ProbeAtlas::sync(const State& state, std::string *error)
{
    if (error) error->clear();
    if (!Renderer::SDLGPU::device()) {
        if (error) *error = "SDL_GPU device is not initialized";
        return false;
    }

    const Quality current_quality = Reflections::quality();
    const ProbeAtlasLayout layout = probeAtlasLayout(
        static_cast<std::uint32_t>(std::min<std::size_t>(state.probes.size(), UINT32_MAX)),
        current_quality);
    const std::uint64_t storage_generation = Models::Internal::textureStorageGeneration();
    const std::uint64_t texture_signature =
        sourceSignature(state, layout, current_quality, storage_generation);
    const std::uint64_t metadata_signature =
        metadataSignature(state, layout, current_quality, storage_generation);

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
            "Horse Reflection Probe Atlas");
        if (!replacement) {
            if (error) *error = "failed to create reflection probe texture atlas";
            return false;
        }

        SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
        if (!command) {
            SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), replacement);
            if (error) *error = "failed to acquire reflection probe upload command buffer";
            return false;
        }

        std::vector<std::uint8_t> fallback(
            static_cast<std::size_t>(layout.width) * static_cast<std::size_t>(layout.height) * 4u,
            0u);
        for (std::size_t index = 3u; index < fallback.size(); index += 4u)
            fallback[index] = 255u;

        bool uploaded = true;
        for (std::uint32_t layer = 0u; layer < layout.layers; ++layer) {
            const std::vector<std::uint8_t> *pixels = &fallback;
            std::vector<std::uint8_t> resampled;

            if (layer < layout.probe_count) {
                const Models::TextureHandle handle = state.probes[layer].texture;
                if (Models::Internal::textureStorageReady(handle)) {
                    const Models::TextureAsset *asset = Models::texture(handle);
                    if (asset && asset->image.width > 0 && asset->image.height > 0 &&
                        !asset->image.rgba.empty())
                    {
                        if (asset->image.width == static_cast<int>(layout.width) &&
                            asset->image.height == static_cast<int>(layout.height))
                        {
                            pixels = &asset->image.rgba;
                        } else {
                            resampled = resampleEquirect(asset->image, layout.width, layout.height);
                            pixels = &resampled;
                        }
                    }
                }
            }

            if (!Renderer::SDLGPU::uploadTextureRgba8Layer(
                    command,
                    replacement,
                    layer,
                    layout.width,
                    layout.height,
                    pixels->data(),
                    pixels->size()))
            {
                uploaded = false;
                break;
            }
        }

        if (uploaded && layout.mip_levels > 1u)
            uploaded = Renderer::SDLGPU::generateMipmaps(command, replacement);

        if (!uploaded || !SDL_SubmitGPUCommandBuffer(command)) {
            SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), replacement);
            if (error) *error = "failed to upload reflection probe texture atlas";
            return false;
        }

        if (texture_) SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), texture_);
        texture_ = replacement;
        source_signature_ = texture_signature;
        texture_storage_generation_ = storage_generation;
        quality_ = current_quality;
    }

    if (!buffer_ || metadata_signature != metadata_signature_) {
        const std::vector<float> data = probeMetadata(state, layout, current_quality);
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
