#ifndef HORSE_RENDERER_INTERNAL_HIZ_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_HIZ_SDLGPU_HPP

#include "Renderer/Visibility/HiZ.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Renderer::Visibility::SDLGPU {

class HiZPyramid {
public:
    HiZPyramid() = default;
    ~HiZPyramid();

    HiZPyramid(const HiZPyramid&) = delete;
    HiZPyramid& operator=(const HiZPyramid&) = delete;

    bool init();
    bool resize(std::uint32_t width, std::uint32_t height);
    bool build(SDL_GPUCommandBuffer *command, SDL_GPUTexture *depth);
    void clear();

    bool ready() const;
    std::size_t levelCount() const;
    SDL_GPUTexture *level(std::size_t index) const;
    HiZ::Extent extent(std::size_t index) const;

private:
    void clearLevels();

    SDL_GPUComputePipeline *pipeline_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    std::vector<SDL_GPUTexture *> levels_;
    std::uint32_t width_ = 0u;
    std::uint32_t height_ = 0u;
};

} // namespace Renderer::Visibility::SDLGPU

#endif
