#include "Renderer/Internal/RasterDrawSubmissionSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::RasterizerSDLGPU {
namespace {

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
}

std::uint64_t drawSignature(const RasterGeometry& geometry)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, geometry.vertexCount());
    hashValue(hash, geometry.draws().size());
    for (const RasterGeometry::DrawRange& draw : geometry.draws()) {
        hashValue(hash, draw.first_vertex);
        hashValue(hash, draw.vertex_count);
        hashValue(hash, draw.material);
        hashValue(hash, draw.camera_layer ? 1u : 0u);
    }
    return hash;
}

} // namespace

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

    if (!index_buffer_ || index_count_ != count) {
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
    }

    const std::uint64_t signature = drawSignature(geometry);
    if (signature == draw_signature_) return true;

    std::vector<Batch> batches;
    for (const RasterGeometry::DrawRange& draw : geometry.draws()) {
        if (draw.vertex_count == 0u) continue;
        const auto found = std::find_if(
            batches.begin(),
            batches.end(),
            [&](const Batch& batch) {
                return batch.material == draw.material &&
                    batch.camera_layer == draw.camera_layer;
            });
        if (found == batches.end()) {
            Batch batch;
            batch.material = draw.material;
            batch.camera_layer = draw.camera_layer;
            batches.push_back(batch);
        }
    }

    std::vector<SDL_GPUIndexedIndirectDrawCommand> commands;
    commands.reserve(geometry.draws().size());
    for (Batch& batch : batches) {
        if (commands.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            if (error) *error = "raster indirect command offset exceeds backend range";
            return false;
        }
        batch.first_command = static_cast<std::uint32_t>(commands.size());

        for (const RasterGeometry::DrawRange& draw : geometry.draws()) {
            if (draw.vertex_count == 0u || draw.material != batch.material ||
                draw.camera_layer != batch.camera_layer)
                continue;
            if (draw.first_vertex > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
                draw.vertex_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            {
                if (error) *error = "raster draw range exceeds 32-bit backend range";
                return false;
            }

            SDL_GPUIndexedIndirectDrawCommand command{};
            command.num_indices = static_cast<Uint32>(draw.vertex_count);
            command.num_instances = 1u;
            command.first_index = static_cast<Uint32>(draw.first_vertex);
            command.vertex_offset = 0;
            command.first_instance = 0u;
            commands.push_back(command);
        }

        const std::size_t command_count = commands.size() - batch.first_command;
        if (command_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            if (error) *error = "raster indirect batch exceeds backend draw-count range";
            return false;
        }
        batch.command_count = static_cast<std::uint32_t>(command_count);
    }

    SDL_GPUBuffer *replacement = nullptr;
    if (!commands.empty()) {
        const std::size_t bytes = commands.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand);
        if (bytes > static_cast<std::size_t>(std::numeric_limits<Uint32>::max())) {
            if (error) *error = "raster indirect buffer exceeds backend size range";
            return false;
        }
        replacement = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_INDIRECT,
            bytes,
            commands.data(),
            "Horse Raster Indirect Commands");
        if (!replacement) {
            if (error) *error = "failed to create raster indirect command buffer";
            return false;
        }
    }

    if (indirect_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), indirect_buffer_);
    indirect_buffer_ = replacement;
    batches_ = std::move(batches);
    draw_signature_ = signature;
    return true;
}

void DrawSubmission::bindIndex(SDL_GPURenderPass *pass) const
{
    if (!pass || !index_buffer_) return;
    const SDL_GPUBufferBinding binding{index_buffer_, 0u};
    SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

bool DrawSubmission::drawIndirect(SDL_GPURenderPass *pass, const Batch& batch) const
{
    if (!pass || !indirect_buffer_ || batch.command_count == 0u) return false;
    const std::size_t offset =
        static_cast<std::size_t>(batch.first_command) * sizeof(SDL_GPUIndexedIndirectDrawCommand);
    if (offset > static_cast<std::size_t>(std::numeric_limits<Uint32>::max())) return false;
    SDL_DrawGPUIndexedPrimitivesIndirect(
        pass,
        indirect_buffer_,
        static_cast<Uint32>(offset),
        batch.command_count);
    return true;
}

void DrawSubmission::clear()
{
    if (SDLGPU::device()) {
        if (indirect_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), indirect_buffer_);
        if (index_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), index_buffer_);
    }
    indirect_buffer_ = nullptr;
    index_buffer_ = nullptr;
    index_count_ = 0u;
    draw_signature_ = UINT64_MAX;
    batches_.clear();
}

} // namespace Renderer::RasterizerSDLGPU
