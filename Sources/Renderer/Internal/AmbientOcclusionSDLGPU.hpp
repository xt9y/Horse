#ifndef HORSE_RENDERER_INTERNAL_AMBIENT_OCCLUSION_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_AMBIENT_OCCLUSION_SDLGPU_HPP

#include "Renderer/AmbientOcclusion/AmbientOcclusion.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <string>

namespace Renderer::AmbientOcclusion::SDLGPU {

class Pass {
public:
    Pass() = default;
    ~Pass();

    Pass(const Pass&) = delete;
    Pass& operator=(const Pass&) = delete;

    bool init();
    bool resize(std::uint32_t width, std::uint32_t height);
    bool build(
        SDL_GPUCommandBuffer *command,
        SDL_GPUTexture *depth,
        SDL_GPUTexture *normal,
        const Renderer::SDLGPU::FrameUniforms& frame,
        const Settings& settings,
        std::string *error = nullptr
    );
    bool bind(
        SDL_GPURenderPass *pass,
        SDL_GPUCommandBuffer *command,
        bool enabled,
        std::uint32_t storage_slot = 6u
    ) const;
    void clear();

    bool ready() const { return ready_; }

private:
    void clearBuffers();

    SDL_GPUComputePipeline *pipeline_ = nullptr;
    SDL_GPUComputePipeline *filter_pipeline_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    SDL_GPUBuffer *raw_ = nullptr;
    SDL_GPUBuffer *filtered_ = nullptr;
    std::uint32_t width_ = 0u;
    std::uint32_t height_ = 0u;
    bool ready_ = false;
};

} // namespace Renderer::AmbientOcclusion::SDLGPU

#endif
