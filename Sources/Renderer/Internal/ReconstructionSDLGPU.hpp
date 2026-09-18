#ifndef HORSE_RENDERER_INTERNAL_RECONSTRUCTION_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_RECONSTRUCTION_SDLGPU_HPP

#include "Renderer/Reconstruction/Reconstruction.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstdint>

namespace Renderer::Internal {

class ReconstructionSDLGPU {
public:
    ReconstructionSDLGPU() = default;
    ~ReconstructionSDLGPU();

    ReconstructionSDLGPU(const ReconstructionSDLGPU&) = delete;
    ReconstructionSDLGPU& operator=(const ReconstructionSDLGPU&) = delete;

    bool resize(int width, int height);
    void reset();
    void shutdown();

    bool resolve(
        SDL_GPUCommandBuffer *command,
        SDL_GPUTexture *fresh_color,
        SDL_GPUTexture *fresh_depth,
        SDL_GPUTexture *fresh_surface,
        SDL_GPUTexture *output_color,
        SDL_GPUTexture *output_depth,
        const Scenes::CameraState& camera,
        const Reconstruction::Settings& settings,
        std::uint32_t frame_index,
        std::uint32_t grid,
        bool camera_moving,
        bool reset_history);

    int width() const { return width_; }
    int height() const { return height_; }
    bool historyValid() const { return history_valid_; }

private:
    bool ensurePipeline();
    bool createHistory();
    void destroyHistory();

    SDL_GPUComputePipeline *pipeline_ = nullptr;
    SDL_GPUSampler *nearest_sampler_ = nullptr;
    std::array<SDL_GPUTexture *, 2> history_color_{};
    std::array<SDL_GPUTexture *, 2> history_depth_{};
    std::array<SDL_GPUTexture *, 2> history_surface_{};
    std::array<SDL_GPUTexture *, 2> history_age_{};
    Scenes::CameraState previous_camera_{};
    std::uint32_t history_index_ = 0u;
    int width_ = 1;
    int height_ = 1;
    bool history_valid_ = false;
};

} // namespace Renderer::Internal

#endif
