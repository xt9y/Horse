#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Renderer::SDLGPU {

SDL_GPUTexture *createTexture(
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_levels,
    const char *label)
{
    if (mip_levels <= 1u)
        return createTexture(format, usage, width, height, label);
    if (!device() || width == 0u || height == 0u) return nullptr;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1u;
    info.num_levels = std::max(mip_levels, 1u);
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_PropertiesID properties = 0;
    if (label) {
        properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, label);
        info.props = properties;
    }
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device(), &info);
    if (properties) SDL_DestroyProperties(properties);
    return texture;
}

SDL_GPUTexture *createTextureArray(
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t layers,
    std::uint32_t mip_levels,
    const char *label)
{
    if (!device() || width == 0u || height == 0u || layers == 0u) return nullptr;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = layers;
    info.num_levels = std::max(mip_levels, 1u);
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_PropertiesID properties = 0;
    if (label) {
        properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, label);
        info.props = properties;
    }
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device(), &info);
    if (properties) SDL_DestroyProperties(properties);
    return texture;
}

bool uploadTextureRgba8Layer(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *texture,
    std::uint32_t layer,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size)
{
    const std::size_t minimum_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (!device() || !command || !texture || !rgba || width == 0u || height == 0u ||
        size < minimum_size || size > std::numeric_limits<Uint32>::max())
        return false;

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(size);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device(), &transfer_info);
    if (!transfer) return false;

    void *mapped = SDL_MapGPUTransferBuffer(device(), transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device(), transfer);
        return false;
    }
    std::memcpy(mapped, rgba, size);
    SDL_UnmapGPUTransferBuffer(device(), transfer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        SDL_ReleaseGPUTransferBuffer(device(), transfer);
        return false;
    }

    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = width;
    source.rows_per_layer = height;

    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.layer = layer;
    destination.w = width;
    destination.h = height;
    destination.d = 1u;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device(), transfer);
    return true;
}

bool generateMipmaps(SDL_GPUCommandBuffer *command, SDL_GPUTexture *texture)
{
    if (!device() || !command || !texture) return false;
    SDL_GenerateMipmapsForGPUTexture(command, texture);
    return true;
}

bool generateMipmaps(SDL_GPUTexture *texture)
{
    if (!device() || !texture) return false;
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(device());
    if (!command) return false;
    SDL_GenerateMipmapsForGPUTexture(command, texture);
    if (!SDL_SubmitGPUCommandBuffer(command)) {
        SDL_CancelGPUCommandBuffer(command);
        return false;
    }
    return true;
}

} // namespace Renderer::SDLGPU
