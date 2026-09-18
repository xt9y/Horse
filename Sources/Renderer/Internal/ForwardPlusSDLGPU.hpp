#ifndef HORSE_RENDERER_INTERNAL_FORWARD_PLUS_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_FORWARD_PLUS_SDLGPU_HPP

#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/Lighting/ForwardPlus.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <string>

namespace Renderer::Lighting::SDLGPU {

class ForwardPlus {
public:
    ForwardPlus() = default;
    ~ForwardPlus();

    ForwardPlus(const ForwardPlus&) = delete;
    ForwardPlus& operator=(const ForwardPlus&) = delete;

    bool init();
    bool resize(std::uint32_t width, std::uint32_t height);
    bool build(
        SDL_GPUCommandBuffer *command,
        const GlobalIllumination::Field *global_illumination,
        const Renderer::SDLGPU::FrameUniforms& frame,
        std::string *error = nullptr
    );
    bool bind(
        SDL_GPURenderPass *pass,
        SDL_GPUCommandBuffer *command,
        bool enabled,
        std::uint32_t slot = 4u
    ) const;
    void clear();

    bool ready() const { return ready_; }
    Renderer::Lighting::ForwardPlus::Grid grid() const { return grid_; }

private:
    SDL_GPUComputePipeline *pipeline_ = nullptr;
    SDL_GPUBuffer *counts_ = nullptr;
    SDL_GPUBuffer *indices_ = nullptr;
    Renderer::Lighting::ForwardPlus::Grid grid_{};
    std::uint32_t width_ = 0u;
    std::uint32_t height_ = 0u;
    bool ready_ = false;
};

} // namespace Renderer::Lighting::SDLGPU

#endif
