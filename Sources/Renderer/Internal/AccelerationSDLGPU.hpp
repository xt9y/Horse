#ifndef HORSE_RENDERER_INTERNAL_ACCELERATION_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_ACCELERATION_SDLGPU_HPP

#include "Renderer/Scenes/Acceleration.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Renderer::Scenes::SDLGPU {

class AccelerationResources {
public:
    AccelerationResources() = default;
    ~AccelerationResources();

    AccelerationResources(const AccelerationResources&) = delete;
    AccelerationResources& operator=(const AccelerationResources&) = delete;

    bool sync(
        const AccelerationScene& acceleration,
        const SceneCache& dynamic_scene,
        std::string *error = nullptr
    );
    void bindCompute(SDL_GPUComputePass *pass, std::uint32_t slot = 0u) const;
    void clear();

    std::size_t tlasNodeCount() const { return tlas_node_count_; }
    std::size_t instanceCount() const { return instance_count_; }
    std::size_t blasNodeCount() const { return blas_node_count_; }
    std::size_t blasCount() const { return blas_count_; }
    std::size_t localTriangleCount() const { return local_triangle_count_; }
    std::size_t dynamicNodeCount() const { return dynamic_node_count_; }
    std::size_t dynamicTriangleCount() const { return dynamic_triangle_count_; }

private:
    bool ensureBuffer(
        SDL_GPUBuffer *&target,
        std::size_t& capacity,
        std::size_t bytes,
        const char *label
    );

    SDL_GPUBuffer *tlas_nodes_ = nullptr;
    SDL_GPUBuffer *instances_ = nullptr;
    SDL_GPUBuffer *blas_nodes_ = nullptr;
    SDL_GPUBuffer *blases_ = nullptr;
    SDL_GPUBuffer *local_triangles_ = nullptr;
    SDL_GPUBuffer *dynamic_nodes_ = nullptr;
    SDL_GPUBuffer *dynamic_triangles_ = nullptr;

    std::size_t tlas_nodes_capacity_ = 0u;
    std::size_t instances_capacity_ = 0u;
    std::size_t blas_nodes_capacity_ = 0u;
    std::size_t blases_capacity_ = 0u;
    std::size_t local_triangles_capacity_ = 0u;
    std::size_t dynamic_nodes_capacity_ = 0u;
    std::size_t dynamic_triangles_capacity_ = 0u;

    std::size_t tlas_node_count_ = 0u;
    std::size_t instance_count_ = 0u;
    std::size_t blas_node_count_ = 0u;
    std::size_t blas_count_ = 0u;
    std::size_t local_triangle_count_ = 0u;
    std::size_t dynamic_node_count_ = 0u;
    std::size_t dynamic_triangle_count_ = 0u;

    std::uint64_t blas_revision_ = UINT64_MAX;
    std::uint64_t tlas_revision_ = UINT64_MAX;
    std::uint64_t dynamic_revision_ = UINT64_MAX;
};

} // namespace Renderer::Scenes::SDLGPU

#endif
