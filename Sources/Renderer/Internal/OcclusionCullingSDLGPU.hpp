#ifndef HORSE_RENDERER_INTERNAL_OCCLUSION_CULLING_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_OCCLUSION_CULLING_SDLGPU_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Internal/HiZSDLGPU.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::Visibility::SDLGPU {

class OcclusionCulling {
public:
    OcclusionCulling() = default;
    ~OcclusionCulling();

    OcclusionCulling(const OcclusionCulling&) = delete;
    OcclusionCulling& operator=(const OcclusionCulling&) = delete;

    bool init();
    bool sync(
        const Ecs::World& world,
        const Scenes::SceneCache& scene,
        std::string *error = nullptr
    );
    bool cull(
        SDL_GPUCommandBuffer *command,
        const HiZPyramid& hi_z,
        SDL_GPUBuffer *visibility,
        const Renderer::SDLGPU::FrameUniforms& frame,
        std::uint32_t width,
        std::uint32_t height
    );
    void clear();

    std::size_t candidateCount() const;

private:
    struct GpuBounds;

    SDL_GPUComputePipeline *pipeline_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    SDL_GPUBuffer *bounds_buffer_ = nullptr;
    std::size_t bounds_capacity_ = 0u;
    std::vector<GpuBounds> bounds_;
    const Ecs::World *world_ = nullptr;
    Scenes::Scene::RenderRevision revision_{};
    bool revision_valid_ = false;
};

} // namespace Renderer::Visibility::SDLGPU

#endif
