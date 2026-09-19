#ifndef HORSE_RENDERER_INTERNAL_REFLECTION_PROBES_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_REFLECTION_PROBES_SDLGPU_HPP

#include "Renderer/Reflections/Reflections.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <string>

namespace Renderer::Reflections::SDLGPU {

class ProbeAtlas {
public:
    ProbeAtlas() = default;
    ~ProbeAtlas();

    ProbeAtlas(const ProbeAtlas&) = delete;
    ProbeAtlas& operator=(const ProbeAtlas&) = delete;

    bool sync(const State& state, std::string *error = nullptr);
    void bind(SDL_GPURenderPass *pass, std::uint32_t slot) const;
    void clear();

    bool ready() const { return texture_ && sampler_; }
    std::uint32_t probeCount() const { return probe_count_; }
    std::uint32_t mipLevels() const { return mip_levels_; }

private:
    SDL_GPUTexture *texture_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    std::uint64_t source_signature_ = UINT64_MAX;
    std::uint64_t texture_storage_generation_ = UINT64_MAX;
    Quality quality_ = Quality::High;
    std::uint32_t probe_count_ = 0u;
    std::uint32_t mip_levels_ = 1u;
};

} // namespace Renderer::Reflections::SDLGPU

#endif
