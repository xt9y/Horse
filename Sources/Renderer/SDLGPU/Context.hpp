#ifndef HORSE_RENDERER_SDLGPU_CONTEXT_HPP
#define HORSE_RENDERER_SDLGPU_CONTEXT_HPP

#include <SDL3/SDL_gpu.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <cstddef>
#include <cstdint>

namespace Renderer::SDLGPU {

bool retain();
void release();
bool initialized();

SDL_GPUDevice *device();
SDL_GPUTextureFormat swapchainFormat();
SDL_GPUTextureFormat colorFormat();
const char *driver();
bool linearSwapchain();

SDL_GPUShader *compileGraphicsShader(
    const char *source,
    SDL_ShaderCross_ShaderStage stage,
    const char *label,
    const char *entrypoint = "main"
);
SDL_GPUComputePipeline *compileComputePipeline(
    const char *source,
    const char *label,
    const char *entrypoint = "main"
);

SDL_GPUBuffer *createBuffer(
    SDL_GPUBufferUsageFlags usage,
    std::size_t size,
    const void *data = nullptr,
    const char *label = nullptr
);
bool uploadBuffer(
    SDL_GPUCommandBuffer *command,
    SDL_GPUBuffer *buffer,
    const void *data,
    std::size_t size,
    bool cycle = true
);

SDL_GPUTexture *createTexture(
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    std::uint32_t width,
    std::uint32_t height,
    const char *label = nullptr
);
SDL_GPUTexture *createTexture(
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_levels,
    const char *label = nullptr
);
SDL_GPUTexture *createTextureArray(
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t layers,
    std::uint32_t mip_levels,
    const char *label = nullptr
);
bool uploadTextureRgba8(
    SDL_GPUTexture *texture,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size
);
bool uploadTextureRgba8(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *texture,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size
);
bool uploadTextureRgba8Subresource(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *texture,
    std::uint32_t layer,
    std::uint32_t mip_level,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size
);
bool uploadTextureRgba8Layer(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *texture,
    std::uint32_t layer,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size
);
bool generateMipmaps(SDL_GPUCommandBuffer *command, SDL_GPUTexture *texture);
bool generateMipmaps(SDL_GPUTexture *texture);

SDL_GPUSampler *createLinearSampler();
SDL_GPUSampler *createNearestSampler();

bool transformColor(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *source,
    SDL_GPUTexture *destination,
    std::uint32_t width,
    std::uint32_t height,
    float exposure_ev,
    std::uint32_t tone_mapping,
    bool encode_srgb
);

bool blitToSwapchain(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *source,
    std::uint32_t source_width,
    std::uint32_t source_height
);

void cancel(SDL_GPUCommandBuffer *command);

} // namespace Renderer::SDLGPU

#endif