#ifndef HORSE_RENDERER_INTERNAL_RASTER_DRAW_SUBMISSION_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_RASTER_DRAW_SUBMISSION_SDLGPU_HPP

#include "Renderer/Internal/RasterGeometrySDLGPU.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::RasterizerSDLGPU {

class DrawSubmission {
public:
    struct Batch {
        Models::MaterialHandle material = Models::INVALID_MATERIAL;
        bool camera_layer = false;
        std::uint32_t first_command = 0u;
        std::uint32_t command_count = 0u;
    };

    DrawSubmission() = default;
    ~DrawSubmission();

    DrawSubmission(const DrawSubmission&) = delete;
    DrawSubmission& operator=(const DrawSubmission&) = delete;

    bool sync(const RasterGeometry& geometry, std::string *error = nullptr);
    void bindIndex(SDL_GPURenderPass *pass) const;
    bool drawIndirect(SDL_GPURenderPass *pass, const Batch& batch) const;
    void clear();

    bool ready() const { return index_buffer_ != nullptr; }
    std::size_t indexCount() const { return index_count_; }
    const std::vector<Batch>& batches() const { return batches_; }

private:
    SDL_GPUBuffer *index_buffer_ = nullptr;
    SDL_GPUBuffer *indirect_buffer_ = nullptr;
    std::size_t index_count_ = 0u;
    std::uint64_t draw_signature_ = UINT64_MAX;
    std::vector<Batch> batches_;
};

} // namespace Renderer::RasterizerSDLGPU

#endif
