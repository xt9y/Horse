#ifndef HORSE_RENDERER_FRAME_SDLGPU_HPP
#define HORSE_RENDERER_FRAME_SDLGPU_HPP

#include "Renderer/Renderer.hpp"

#include <SDL3/SDL_gpu.h>

namespace Renderer::Frame::SDLGPU {

class Target {
public:
    Target() = default;
    ~Target();

    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    bool resize(int width, int height);
    bool begin(Internal::FrameOutput& output);
    void shutdown();

    SDL_GPUTexture *color() const { return color_; }
    SDL_GPUTexture *depth() const { return depth_; }
    SDL_GPUTexture *linearDepth() const { return linear_depth_; }
    SDL_GPUTexture *velocity() const { return velocity_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    bool create();
    void destroy();

    SDL_GPUTexture *color_ = nullptr;
    SDL_GPUTexture *depth_ = nullptr;
    SDL_GPUTexture *linear_depth_ = nullptr;
    SDL_GPUTexture *velocity_ = nullptr;
    int width_ = 1;
    int height_ = 1;
};

bool compose(Internal::FrameOutput& output);
void present(Internal::FrameOutput& output);
void cancel(Internal::FrameOutput& output);

} // namespace Renderer::Frame::SDLGPU

#endif
