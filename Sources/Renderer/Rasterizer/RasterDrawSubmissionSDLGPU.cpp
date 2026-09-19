#include "Renderer/Internal/RasterDrawSubmissionSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::RasterizerSDLGPU {
namespace {

struct alignas(16) DrawMeta {
    std::uint32_t first_index = 0u;
    std::uint32_t num_indices = 0u;
    std::uint32_t entity = 0u;
    std::uint32_t batch = 0u;
};

struct alignas(16) BatchMeta {
    std::uint32_t first_index = 0u;
    std::uint32_t capacity = 0u;
    std::uint32_t reserved0 = 0u;
    std::uint32_t reserved1 = 0u;
};

static_assert(sizeof(DrawMeta) == 16u);
static_assert(sizeof(BatchMeta) == 16u);
static_assert(sizeof(SDL_GPUIndexedIndirectDrawCommand) == 20u);

inline constexpr const char *ResetShader = R"HLSL(
struct DrawCommand {
    uint num_indices;
    uint num_instances;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

StructuredBuffer<uint4> Batches : register(t0, space0);
RWStructuredBuffer<DrawCommand> Commands : register(u0, space1);
cbuffer Info : register(b0, space2) { uint4 Params; };

[numthreads(64, 1, 1)]
void Main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Params.x) return;
    DrawCommand command;
    command.num_indices = 0u;
    command.num_instances = 1u;
    command.first_index = Batches[id.x].x;
    command.vertex_offset = 0;
    command.first_instance = 0u;
    Commands[id.x] = command;
}
)HLSL";

inline constexpr const char *CompactShader = R"HLSL(
struct DrawCommand {
    uint num_indices;
    uint num_instances;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

StructuredBuffer<uint4> Draws : register(t0, space0);
StructuredBuffer<uint> Visibility : register(t1, space0);
StructuredBuffer<uint4> Batches : register(t2, space0);
RWStructuredBuffer<uint> Indices : register(u0, space1);
RWStructuredBuffer<DrawCommand> Commands : register(u1, space1);
cbuffer Info : register(b0, space2) { uint4 Params; };

groupshared uint GroupBase;
groupshared uint GroupCount;

[numthreads(64, 1, 1)]
void Main(uint3 group_id : SV_GroupID, uint3 group_thread : SV_GroupThreadID)
{
    uint draw_index = group_id.x;
    if (draw_index >= Params.x) return;
    uint4 draw = Draws[draw_index];

    if (group_thread.x == 0u) {
        GroupBase = 0u;
        GroupCount = 0u;
        if (Visibility[draw.z] != 0u) {
            InterlockedAdd(Commands[draw.w].num_indices, draw.y, GroupBase);
            GroupCount = draw.y;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    uint first_output = Batches[draw.w].x + GroupBase;
    for (uint index = group_thread.x; index < GroupCount; index += 64u)
        Indices[first_output + index] = draw.x + index;
}
)HLSL";

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
}

std::uint64_t drawSignature(const RasterGeometry& geometry)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, geometry.vertexCount());
    hashValue(hash, geometry.drawItems().size());
    for (const RasterGeometry::DrawItem& draw : geometry.drawItems()) {
        hashValue(hash, draw.first_vertex);
        hashValue(hash, draw.vertex_count);
        hashValue(hash, draw.material);
        hashValue(hash, draw.entity);
        hashValue(hash, draw.camera_layer ? 1u : 0u);
    }
    return hash;
}

bool fail(std::string *error, const char *message)
{
    if (error) *error = message;
    return false;
}

} // namespace

DrawSubmission::~DrawSubmission()
{
    clear();
}

bool DrawSubmission::sync(const RasterGeometry& geometry, std::string *error)
{
    if (error) error->clear();
    compacted_ = false;
    if (!SDLGPU::device()) return fail(error, "SDL_GPU device is not initialized");

    const std::size_t count = geometry.vertexCount();
    if (count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        return fail(error, "raster index buffer exceeds 32-bit backend index range");

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
        if (!replacement) return fail(error, "failed to create raster index buffer");

        if (index_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), index_buffer_);
        index_buffer_ = replacement;
        index_count_ = count;
    }

    const std::uint64_t signature = drawSignature(geometry);
    if (signature == draw_signature_) return true;

    std::vector<Batch> batches;
    for (const RasterGeometry::DrawItem& draw : geometry.drawItems()) {
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
    commands.reserve(geometry.drawItems().size());
    for (Batch& batch : batches) {
        if (commands.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            return fail(error, "raster indirect command offset exceeds backend range");
        batch.first_command = static_cast<std::uint32_t>(commands.size());

        for (const RasterGeometry::DrawItem& draw : geometry.drawItems()) {
            if (draw.vertex_count == 0u || draw.material != batch.material ||
                draw.camera_layer != batch.camera_layer)
                continue;
            if (draw.first_vertex > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
                draw.vertex_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
                return fail(error, "raster draw range exceeds 32-bit backend range");

            SDL_GPUIndexedIndirectDrawCommand command{};
            command.num_indices = static_cast<Uint32>(draw.vertex_count);
            command.num_instances = 1u;
            command.first_index = static_cast<Uint32>(draw.first_vertex);
            command.vertex_offset = 0;
            command.first_instance = 0u;
            commands.push_back(command);
        }

        const std::size_t command_count = commands.size() - batch.first_command;
        if (command_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            return fail(error, "raster indirect batch exceeds backend draw-count range");
        batch.command_count = static_cast<std::uint32_t>(command_count);
    }

    std::vector<BatchMeta> batch_meta;
    std::uint32_t compact_first_index = 0u;
    for (Batch& batch : batches) {
        if (batch.camera_layer) continue;
        std::uint64_t capacity = 0u;
        for (const RasterGeometry::DrawItem& draw : geometry.drawItems()) {
            if (!draw.camera_layer && draw.material == batch.material)
                capacity += draw.vertex_count;
        }
        if (capacity > std::numeric_limits<std::uint32_t>::max() ||
            static_cast<std::uint64_t>(compact_first_index) + capacity >
                std::numeric_limits<std::uint32_t>::max())
            return fail(error, "raster compact index range exceeds 32-bit backend range");

        batch.compact_command = static_cast<std::uint32_t>(batch_meta.size());
        batch_meta.push_back(BatchMeta{
            compact_first_index,
            static_cast<std::uint32_t>(capacity),
            0u,
            0u,
        });
        compact_first_index += static_cast<std::uint32_t>(capacity);
    }

    std::vector<DrawMeta> draw_meta;
    draw_meta.reserve(geometry.drawItems().size());
    for (const RasterGeometry::DrawItem& draw : geometry.drawItems()) {
        if (draw.camera_layer || draw.vertex_count == 0u) continue;
        const auto batch = std::find_if(
            batches.begin(),
            batches.end(),
            [&](const Batch& value) {
                return !value.camera_layer && value.material == draw.material;
            });
        if (batch == batches.end() || batch->compact_command == UINT32_MAX)
            return fail(error, "raster compact draw has no material batch");
        if (draw.first_vertex > std::numeric_limits<std::uint32_t>::max() ||
            draw.vertex_count > std::numeric_limits<std::uint32_t>::max())
            return fail(error, "raster compact draw exceeds 32-bit backend range");
        draw_meta.push_back(DrawMeta{
            static_cast<std::uint32_t>(draw.first_vertex),
            static_cast<std::uint32_t>(draw.vertex_count),
            static_cast<std::uint32_t>(draw.entity),
            batch->compact_command,
        });
    }

    SDL_GPUBuffer *replacement_indirect = nullptr;
    SDL_GPUBuffer *replacement_draw_meta = nullptr;
    SDL_GPUBuffer *replacement_batch_meta = nullptr;
    SDL_GPUBuffer *replacement_compact_index = nullptr;
    SDL_GPUBuffer *replacement_compact_indirect = nullptr;

    if (!commands.empty()) {
        replacement_indirect = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_INDIRECT,
            commands.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand),
            commands.data(),
            "Horse Raster Indirect Commands");
        if (!replacement_indirect)
            return fail(error, "failed to create raster indirect command buffer");
    }

    if (!draw_meta.empty()) {
        replacement_draw_meta = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
            draw_meta.size() * sizeof(DrawMeta),
            draw_meta.data(),
            "Horse Raster Draw Metadata");
        replacement_batch_meta = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
            batch_meta.size() * sizeof(BatchMeta),
            batch_meta.data(),
            "Horse Raster Batch Metadata");
        replacement_compact_index = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_INDEX | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
            std::max<std::size_t>(compact_first_index, 1u) * sizeof(std::uint32_t),
            nullptr,
            "Horse Raster Compact Indices");
        replacement_compact_indirect = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
            batch_meta.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand),
            nullptr,
            "Horse Raster Compact Commands");
        if (!replacement_draw_meta || !replacement_batch_meta ||
            !replacement_compact_index || !replacement_compact_indirect)
        {
            if (replacement_compact_indirect) SDL_ReleaseGPUBuffer(SDLGPU::device(), replacement_compact_indirect);
            if (replacement_compact_index) SDL_ReleaseGPUBuffer(SDLGPU::device(), replacement_compact_index);
            if (replacement_batch_meta) SDL_ReleaseGPUBuffer(SDLGPU::device(), replacement_batch_meta);
            if (replacement_draw_meta) SDL_ReleaseGPUBuffer(SDLGPU::device(), replacement_draw_meta);
            if (replacement_indirect) SDL_ReleaseGPUBuffer(SDLGPU::device(), replacement_indirect);
            return fail(error, "failed to create raster compaction buffers");
        }
    }

    if (indirect_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), indirect_buffer_);
    if (compact_indirect_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), compact_indirect_buffer_);
    if (compact_index_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), compact_index_buffer_);
    if (batch_meta_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), batch_meta_buffer_);
    if (draw_meta_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), draw_meta_buffer_);

    indirect_buffer_ = replacement_indirect;
    draw_meta_buffer_ = replacement_draw_meta;
    batch_meta_buffer_ = replacement_batch_meta;
    compact_index_buffer_ = replacement_compact_index;
    compact_indirect_buffer_ = replacement_compact_indirect;
    compact_index_capacity_ = compact_first_index;
    compact_draw_count_ = static_cast<std::uint32_t>(draw_meta.size());
    compact_batch_count_ = static_cast<std::uint32_t>(batch_meta.size());
    batches_ = std::move(batches);
    draw_signature_ = signature;
    return true;
}

bool DrawSubmission::compact(
    SDL_GPUCommandBuffer *command,
    SDL_GPUBuffer *visibility,
    std::string *error)
{
    if (error) error->clear();
    compacted_ = false;
    if (compaction_disabled_) return true;
    if (compact_draw_count_ == 0u || compact_batch_count_ == 0u) return true;
    if (!command || !visibility || !draw_meta_buffer_ || !batch_meta_buffer_ ||
        !compact_index_buffer_ || !compact_indirect_buffer_)
        return fail(error, "raster compaction resources are incomplete");

    if (!reset_pipeline_ || !compact_pipeline_) {
        if (compaction_attempted_) {
            compaction_disabled_ = true;
            return fail(error, "raster compaction pipeline initialization previously failed");
        }
        compaction_attempted_ = true;
        reset_pipeline_ = SDLGPU::compileComputePipeline(
            ResetShader, "Horse Raster Draw Reset", "Main");
        compact_pipeline_ = SDLGPU::compileComputePipeline(
            CompactShader, "Horse Raster Draw Compact", "Main");
        if (!reset_pipeline_ || !compact_pipeline_) {
            compaction_disabled_ = true;
            return fail(error, "failed to create raster compaction compute pipelines");
        }
    }

    const std::array<std::uint32_t, 4> reset_info{
        compact_batch_count_, 0u, 0u, 0u,
    };
    SDL_PushGPUComputeUniformData(command, 0u, reset_info.data(), sizeof(reset_info));
    SDL_GPUStorageBufferReadWriteBinding reset_writable{};
    reset_writable.buffer = compact_indirect_buffer_;
    reset_writable.cycle = false;
    SDL_GPUComputePass *reset_pass = SDL_BeginGPUComputePass(
        command, nullptr, 0u, &reset_writable, 1u);
    if (!reset_pass) {
        compaction_disabled_ = true;
        return fail(error, "failed to begin raster command reset pass");
    }
    SDL_BindGPUComputePipeline(reset_pass, reset_pipeline_);
    SDL_GPUBuffer *reset_read[] = {batch_meta_buffer_};
    SDL_BindGPUComputeStorageBuffers(reset_pass, 0u, reset_read, 1u);
    SDL_DispatchGPUCompute(reset_pass, (compact_batch_count_ + 63u) / 64u, 1u, 1u);
    SDL_EndGPUComputePass(reset_pass);

    const std::array<std::uint32_t, 4> compact_info{
        compact_draw_count_, compact_batch_count_, 0u, 0u,
    };
    SDL_PushGPUComputeUniformData(command, 0u, compact_info.data(), sizeof(compact_info));
    SDL_GPUStorageBufferReadWriteBinding writable[2]{};
    writable[0].buffer = compact_index_buffer_;
    writable[0].cycle = false;
    writable[1].buffer = compact_indirect_buffer_;
    writable[1].cycle = false;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
        command, nullptr, 0u, writable, 2u);
    if (!pass) {
        compaction_disabled_ = true;
        return fail(error, "failed to begin raster draw compaction pass");
    }
    SDL_BindGPUComputePipeline(pass, compact_pipeline_);
    SDL_GPUBuffer *read[] = {draw_meta_buffer_, visibility, batch_meta_buffer_};
    SDL_BindGPUComputeStorageBuffers(pass, 0u, read, 3u);
    SDL_DispatchGPUCompute(pass, compact_draw_count_, 1u, 1u);
    SDL_EndGPUComputePass(pass);

    compacted_ = true;
    return true;
}

void DrawSubmission::bindIndex(SDL_GPURenderPass *pass, bool compact_world) const
{
    if (!pass) return;
    SDL_GPUBuffer *buffer = compact_world && compacted_
        ? compact_index_buffer_
        : index_buffer_;
    if (!buffer) return;
    const SDL_GPUBufferBinding binding{buffer, 0u};
    SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

bool DrawSubmission::drawIndirect(
    SDL_GPURenderPass *pass,
    const Batch& batch,
    bool compact_world) const
{
    if (!pass) return false;
    if (compact_world && compacted_ && !batch.camera_layer &&
        batch.compact_command != UINT32_MAX)
    {
        if (!compact_indirect_buffer_) return false;
        const std::size_t offset =
            static_cast<std::size_t>(batch.compact_command) *
            sizeof(SDL_GPUIndexedIndirectDrawCommand);
        if (offset > static_cast<std::size_t>(std::numeric_limits<Uint32>::max())) return false;
        SDL_DrawGPUIndexedPrimitivesIndirect(
            pass,
            compact_indirect_buffer_,
            static_cast<Uint32>(offset),
            1u);
        return true;
    }

    if (!indirect_buffer_ || batch.command_count == 0u) return false;
    const std::size_t offset =
        static_cast<std::size_t>(batch.first_command) *
        sizeof(SDL_GPUIndexedIndirectDrawCommand);
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
        if (compact_pipeline_) SDL_ReleaseGPUComputePipeline(SDLGPU::device(), compact_pipeline_);
        if (reset_pipeline_) SDL_ReleaseGPUComputePipeline(SDLGPU::device(), reset_pipeline_);
        if (compact_indirect_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), compact_indirect_buffer_);
        if (compact_index_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), compact_index_buffer_);
        if (batch_meta_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), batch_meta_buffer_);
        if (draw_meta_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), draw_meta_buffer_);
        if (indirect_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), indirect_buffer_);
        if (index_buffer_) SDL_ReleaseGPUBuffer(SDLGPU::device(), index_buffer_);
    }
    compact_pipeline_ = nullptr;
    reset_pipeline_ = nullptr;
    compact_indirect_buffer_ = nullptr;
    compact_index_buffer_ = nullptr;
    batch_meta_buffer_ = nullptr;
    draw_meta_buffer_ = nullptr;
    indirect_buffer_ = nullptr;
    index_buffer_ = nullptr;
    index_count_ = 0u;
    compact_index_capacity_ = 0u;
    compact_draw_count_ = 0u;
    compact_batch_count_ = 0u;
    draw_signature_ = UINT64_MAX;
    compaction_attempted_ = false;
    compaction_disabled_ = false;
    compacted_ = false;
    batches_.clear();
}

} // namespace Renderer::RasterizerSDLGPU
