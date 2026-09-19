#ifndef HORSE_RENDERER_INTERNAL_RASTER_DRAW_SUBMISSION_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_RASTER_DRAW_SUBMISSION_SDLGPU_HPP

#include "Renderer/Internal/RasterGeometrySDLGPU.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <string>

namespace Renderer::RasterizerSDLGPU {

class DrawSubmission {
public:
    DrawSubmission() = default;
    ~DrawSubmission();

    DrawSubmission(const DrawSubmission&) = delete;
    DrawSubmission& operator=(const DrawSubmission&) = delete;

    bool sync(const RasterGeometry& geometry, std::string *error = nullptr);
    void bindIndex(SDL_GPURenderPass *pass) const;
    void clear();

    bool ready() const { return index_buffer_ != nullptr; }
    std::size_t indexCount() const { return index_count_; }

private:
    SDL_GPUBuffer *index_buffer_ = nullptr;
    std::size_t index_count_ = 0u;
};

} // namespace Renderer::RasterizerSDLGPU

#endif
