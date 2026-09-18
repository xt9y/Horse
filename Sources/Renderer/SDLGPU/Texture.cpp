#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

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
