#include "Renderer/Internal/RasterDrawSubmissionSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::RasterizerSDLGPU {

DrawSubmission::~DrawSubmission()
{
    clear();
}

bool DrawSubmission::sync(const RasterGeometry& geometry, std::string *error)
{
    if (error) error->clear();
    if (!SDLGPU::device()) {
        if (error) *error = "SDL_GPU device is not initialized";
        return false;
    }

    const std::size_t count = geometry.vertexCount();
    if (count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        if (error) *error = "raster index buffer exceeds 32-bit backend index range";
        return false;
    }
    if (index_buffer_ && index_count_ == count) return true;

    const std::size_t safe_count = count == 0u ? 1u : count;
    std::vector<std::uint32_t> indices(safe_count, 0u);
    for (std::size_t index = 0u; index < count; ++index)
        indices[index] = static_cast<std::uint32_t>(index);

    SDL_GPUBuffer *replacement = SDLGPU::createBuffer(
        SDL_GPU_BUFFERUSAGE_INDEX,
        indices.size() * sizeof(std::uint32_t),
        indices.data(),
        "Horse Raster Indices");
    if (!replacement) {
        if (error) *error = "failed to create raster index buffer";
        return false;
    }

    if (index_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), index_buffer_);
    index_buffer_ = replacement;
    index_count_ = count;
    return true;
}

void DrawSubmission::bindIndex(SDL_GPURenderPass *pass) const
{
    if (!pass || !index_buffer_) return;
    const SDL_GPUBufferBinding binding{index_buffer_, 0u};
    SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void DrawSubmission::clear()
{
    if (index_buffer_ && SDLGPU::device())
        SDL_ReleaseGPUBuffer(SDLGPU::device(), index_buffer_);
    index_buffer_ = nullptr;
    index_count_ = 0u;
}

} // namespace Renderer::RasterizerSDLGPU
