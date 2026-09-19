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
        bool gpu_eligible = false;
        std::uint32_t first_command = 0u;
        std::uint32_t command_count = 0u;
        std::uint32_t compact_command = UINT32_MAX;
    };

    DrawSubmission() = default;
    ~DrawSubmission();

    DrawSubmission(const DrawSubmission&) = delete;
    DrawSubmission& operator=(const DrawSubmission&) = delete;

    bool sync(const RasterGeometry& geometry, std::string *error = nullptr);
    bool compact(
        SDL_GPUCommandBuffer *command,
        SDL_GPUBuffer *visibility,
        std::string *error = nullptr
    );
    void bindIndex(SDL_GPURenderPass *pass, bool compact_world = false) const;
    bool drawIndirect(
        SDL_GPURenderPass *pass,
        const Batch& batch,
        bool compact_world = false
    ) const;
    void clear();

    bool ready() const { return index_buffer_ != nullptr; }
    bool compacted() const { return compacted_; }
    std::size_t indexCount() const { return index_count_; }
    const std::vector<Batch>& batches() const { return batches_; }

private:
    SDL_GPUBuffer *index_buffer_ = nullptr;
    SDL_GPUBuffer *indirect_buffer_ = nullptr;
    SDL_GPUBuffer *compact_index_buffer_ = nullptr;
    SDL_GPUBuffer *compact_indirect_buffer_ = nullptr;
    SDL_GPUBuffer *draw_meta_buffer_ = nullptr;
    SDL_GPUBuffer *batch_meta_buffer_ = nullptr;
    SDL_GPUComputePipeline *reset_pipeline_ = nullptr;
    SDL_GPUComputePipeline *compact_pipeline_ = nullptr;
    std::size_t index_count_ = 0u;
    std::size_t compact_index_capacity_ = 0u;
    std::uint32_t compact_draw_count_ = 0u;
    std::uint32_t compact_batch_count_ = 0u;
    std::uint64_t draw_signature_ = UINT64_MAX;
    bool compaction_attempted_ = false;
    bool compaction_disabled_ = false;
    bool compacted_ = false;
    std::vector<Batch> batches_;
};

} // namespace Renderer::RasterizerSDLGPU

#endif
