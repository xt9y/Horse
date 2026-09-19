#include "Renderer/Internal/AccelerationSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace Renderer::Scenes::SDLGPU {
namespace {

constexpr SDL_GPUBufferUsageFlags AccelerationBufferUsage =
    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;

} // namespace

AccelerationResources::~AccelerationResources()
{
    clear();
}

bool AccelerationResources::ensureBuffer(
    SDL_GPUBuffer *&target,
    std::size_t& capacity,
    std::size_t bytes,
    const char *label)
{
    const std::size_t safe_bytes = std::max<std::size_t>(bytes, 16u);
    if (target && capacity >= safe_bytes) return true;

    SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
        AccelerationBufferUsage,
        safe_bytes,
        nullptr,
        label
    );
    if (!replacement) return false;

    if (target) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), target);
    target = replacement;
    capacity = safe_bytes;
    return true;
}

bool AccelerationResources::sync(
    const AccelerationScene& acceleration,
    const SceneCache& dynamic_scene,
    std::string *error)
{
    if (error) error->clear();
    if (!Renderer::SDLGPU::device()) {
        if (error) *error = "SDL_GPU device is not initialized";
        return false;
    }

    const bool blas_changed = blas_revision_ != acceleration.blasRevision();
    const bool tlas_changed = tlas_revision_ != acceleration.tlasRevision();
    const bool dynamic_changed = dynamic_revision_ != dynamic_scene.geometryRevision();
    if (!blas_changed && !tlas_changed && !dynamic_changed) return true;

    const std::size_t tlas_node_bytes = acceleration.tlasNodes().size() * sizeof(GpuNode);
    const std::size_t instance_bytes = acceleration.instances().size() * sizeof(AccelerationInstance);
    const std::size_t blas_node_bytes = acceleration.blasNodes().size() * sizeof(GpuNode);
    const std::size_t blas_bytes = acceleration.blases().size() * sizeof(std::array<std::uint32_t, 4>);
    const std::size_t local_triangle_bytes =
        acceleration.localTriangles().size() * sizeof(AccelerationTriangle);
    const std::size_t dynamic_node_bytes = dynamic_scene.nodes().size() * sizeof(GpuNode);
    const std::size_t dynamic_triangle_bytes =
        dynamic_scene.triangles().size() * sizeof(GpuTriangle);

    if (tlas_changed &&
        (!ensureBuffer(
             tlas_nodes_, tlas_nodes_capacity_, tlas_node_bytes,
             "Horse TLAS Nodes") ||
         !ensureBuffer(
             instances_, instances_capacity_, instance_bytes,
             "Horse TLAS Instances")))
    {
        if (error) *error = "failed to allocate SDL_GPU TLAS buffers";
        return false;
    }

    if (blas_changed &&
        (!ensureBuffer(
             blas_nodes_, blas_nodes_capacity_, blas_node_bytes,
             "Horse BLAS Nodes") ||
         !ensureBuffer(
             blases_, blases_capacity_, blas_bytes,
             "Horse BLAS Metadata") ||
         !ensureBuffer(
             local_triangles_, local_triangles_capacity_, local_triangle_bytes,
             "Horse BLAS Triangles")))
    {
        if (error) *error = "failed to allocate SDL_GPU BLAS buffers";
        return false;
    }

    if (dynamic_changed &&
        (!ensureBuffer(
             dynamic_nodes_, dynamic_nodes_capacity_, dynamic_node_bytes,
             "Horse Dynamic BVH Nodes") ||
         !ensureBuffer(
             dynamic_triangles_, dynamic_triangles_capacity_, dynamic_triangle_bytes,
             "Horse Dynamic Triangles")))
    {
        if (error) *error = "failed to allocate SDL_GPU dynamic trace buffers";
        return false;
    }

    std::vector<std::array<std::uint32_t, 4>> gpu_blases;
    if (blas_changed) {
        gpu_blases.reserve(acceleration.blases().size());
        for (const AccelerationBlas& blas : acceleration.blases()) {
            gpu_blases.push_back({
                blas.node_offset,
                blas.node_count,
                blas.triangle_offset,
                blas.triangle_count,
            });
        }
    }

    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
    if (!command) {
        if (error) *error = "failed to acquire SDL_GPU acceleration upload command buffer";
        return false;
    }

    bool uploaded = true;
    if (tlas_changed) {
        uploaded = uploaded &&
            (tlas_node_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                tlas_nodes_,
                acceleration.tlasNodes().data(),
                tlas_node_bytes,
                true
            ));
        uploaded = uploaded &&
            (instance_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                instances_,
                acceleration.instances().data(),
                instance_bytes,
                true
            ));
    }

    if (blas_changed) {
        uploaded = uploaded &&
            (blas_node_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                blas_nodes_,
                acceleration.blasNodes().data(),
                blas_node_bytes,
                true
            ));
        uploaded = uploaded &&
            (blas_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                blases_,
                gpu_blases.data(),
                blas_bytes,
                true
            ));
        uploaded = uploaded &&
            (local_triangle_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                local_triangles_,
                acceleration.localTriangles().data(),
                local_triangle_bytes,
                true
            ));
    }

    if (dynamic_changed) {
        uploaded = uploaded &&
            (dynamic_node_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                dynamic_nodes_,
                dynamic_scene.nodes().data(),
                dynamic_node_bytes,
                true
            ));
        uploaded = uploaded &&
            (dynamic_triangle_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command,
                dynamic_triangles_,
                dynamic_scene.triangles().data(),
                dynamic_triangle_bytes,
                true
            ));
    }

    if (!uploaded || !SDL_SubmitGPUCommandBuffer(command)) {
        SDL_CancelGPUCommandBuffer(command);
        if (error) *error = "failed to upload SDL_GPU acceleration buffers";
        return false;
    }

    if (blas_changed) blas_revision_ = acceleration.blasRevision();
    if (tlas_changed) tlas_revision_ = acceleration.tlasRevision();
    if (dynamic_changed) dynamic_revision_ = dynamic_scene.geometryRevision();

    tlas_node_count_ = acceleration.tlasNodes().size();
    instance_count_ = acceleration.instances().size();
    blas_node_count_ = acceleration.blasNodes().size();
    blas_count_ = acceleration.blases().size();
    local_triangle_count_ = acceleration.localTriangles().size();
    dynamic_node_count_ = dynamic_scene.nodes().size();
    dynamic_triangle_count_ = dynamic_scene.triangles().size();
    return true;
}

void AccelerationResources::bindCompute(SDL_GPUComputePass *pass, std::uint32_t slot) const
{
    if (!pass || !tlas_nodes_ || !instances_ || !blas_nodes_ || !blases_ ||
        !local_triangles_ || !dynamic_nodes_ || !dynamic_triangles_)
        return;

    SDL_GPUBuffer *buffers[] = {
        tlas_nodes_,
        instances_,
        blas_nodes_,
        blases_,
        local_triangles_,
        dynamic_nodes_,
        dynamic_triangles_,
    };
    SDL_BindGPUComputeStorageBuffers(pass, slot, buffers, 7u);
}

void AccelerationResources::clear()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (dynamic_triangles_) SDL_ReleaseGPUBuffer(device, dynamic_triangles_);
        if (dynamic_nodes_) SDL_ReleaseGPUBuffer(device, dynamic_nodes_);
        if (local_triangles_) SDL_ReleaseGPUBuffer(device, local_triangles_);
        if (blases_) SDL_ReleaseGPUBuffer(device, blases_);
        if (blas_nodes_) SDL_ReleaseGPUBuffer(device, blas_nodes_);
        if (instances_) SDL_ReleaseGPUBuffer(device, instances_);
        if (tlas_nodes_) SDL_ReleaseGPUBuffer(device, tlas_nodes_);
    }

    tlas_nodes_ = nullptr;
    instances_ = nullptr;
    blas_nodes_ = nullptr;
    blases_ = nullptr;
    local_triangles_ = nullptr;
    dynamic_nodes_ = nullptr;
    dynamic_triangles_ = nullptr;

    tlas_nodes_capacity_ = 0u;
    instances_capacity_ = 0u;
    blas_nodes_capacity_ = 0u;
    blases_capacity_ = 0u;
    local_triangles_capacity_ = 0u;
    dynamic_nodes_capacity_ = 0u;
    dynamic_triangles_capacity_ = 0u;

    tlas_node_count_ = 0u;
    instance_count_ = 0u;
    blas_node_count_ = 0u;
    blas_count_ = 0u;
    local_triangle_count_ = 0u;
    dynamic_node_count_ = 0u;
    dynamic_triangle_count_ = 0u;

    blas_revision_ = UINT64_MAX;
    tlas_revision_ = UINT64_MAX;
    dynamic_revision_ = UINT64_MAX;
}

} // namespace Renderer::Scenes::SDLGPU
