#include "Renderer/Frame/FrameSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <cstdio>

namespace Renderer::Frame::SDLGPU {
namespace {

constexpr SDL_GPUTextureUsageFlags ColorUsage =
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
constexpr SDL_GPUTextureUsageFlags VelocityUsage =
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
    SDL_GPU_TEXTUREUSAGE_SAMPLER;
constexpr SDL_GPUTextureUsageFlags LinearDepthUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

bool supported(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage)
{
    return SDL_GPUTextureSupportsFormat(
        Renderer::SDLGPU::device(), format, SDL_GPU_TEXTURETYPE_2D, usage);
}

} // namespace

Target::~Target()
{
    shutdown();
}

bool Target::create()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (!device) return false;

    if (!supported(Renderer::SDLGPU::colorFormat(), ColorUsage) ||
        !supported(SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, VelocityUsage) ||
        !supported(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) ||
        !supported(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, LinearDepthUsage))
    {
        std::fprintf(stderr, "[Frame/SDL_GPU]: required render-target formats are unavailable\n");
        return false;
    }

    color_ = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(), ColorUsage,
        static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_),
        "Horse HDR Color");
    velocity_ = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, VelocityUsage,
        static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_),
        "Horse Velocity");
    depth_ = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_),
        "Horse Depth");
    linear_depth_ = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R32_FLOAT, LinearDepthUsage,
        static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_),
        "Horse Linear Depth");

    if (!color_ || !velocity_ || !depth_ || !linear_depth_) {
        std::fprintf(stderr, "[Frame/SDL_GPU]: target creation failed: %s\n", SDL_GetError());
        destroy();
        return false;
    }
    return true;
}

void Target::destroy()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (linear_depth_) SDL_ReleaseGPUTexture(device, linear_depth_);
        if (depth_) SDL_ReleaseGPUTexture(device, depth_);
        if (velocity_) SDL_ReleaseGPUTexture(device, velocity_);
        if (color_) SDL_ReleaseGPUTexture(device, color_);
    }
    color_ = nullptr;
    velocity_ = nullptr;
    depth_ = nullptr;
    linear_depth_ = nullptr;
}

bool Target::resize(int width, int height)
{
    const int next_width = std::max(width, 1);
    const int next_height = std::max(height, 1);
    if (next_width == width_ && next_height == height_ && color_) return true;
    width_ = next_width;
    height_ = next_height;
    destroy();
    return create();
}

bool Target::begin(Internal::FrameOutput& output)
{
    if (!color_ && !create()) return false;
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
    if (!command) {
        std::fprintf(stderr, "[Frame/SDL_GPU]: command buffer acquisition failed: %s\n", SDL_GetError());
        return false;
    }

    output.api = Internal::GraphicsApi::SDLGPU;
    output.depth = Internal::DepthSource::Native;
    output.width = width_;
    output.height = height_;
    output.command = command;
    output.color_texture = color_;
    output.depth_texture = depth_;
    output.velocity_texture = velocity_;
    return true;
}

void Target::shutdown()
{
    destroy();
}

bool compose(Internal::FrameOutput& output)
{
    return output.api == Internal::GraphicsApi::SDLGPU && output.command && output.color_texture;
}

void present(Internal::FrameOutput& output)
{
    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    auto *color = static_cast<SDL_GPUTexture *>(output.color_texture);
    if (!command) return;

    if (!color || !Renderer::SDLGPU::blitToSwapchain(
            command, color,
            static_cast<std::uint32_t>(std::max(output.width, 1)),
            static_cast<std::uint32_t>(std::max(output.height, 1))))
    {
        Renderer::SDLGPU::cancel(command);
        output.command = nullptr;
        return;
    }

    if (!SDL_SubmitGPUCommandBuffer(command))
        std::fprintf(stderr, "[Frame/SDL_GPU]: submit failed: %s\n", SDL_GetError());
    output.command = nullptr;
}

void cancel(Internal::FrameOutput& output)
{
    Renderer::SDLGPU::cancel(static_cast<SDL_GPUCommandBuffer *>(output.command));
    output.command = nullptr;
}

} // namespace Renderer::Frame::SDLGPU
