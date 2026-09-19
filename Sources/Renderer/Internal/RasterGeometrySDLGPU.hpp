#ifndef HORSE_RENDERER_INTERNAL_RASTER_GEOMETRY_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_RASTER_GEOMETRY_SDLGPU_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::RasterizerSDLGPU {

class RasterGeometry {
public:
    struct DrawRange {
        std::size_t first_vertex = 0u;
        std::size_t vertex_count = 0u;
        Models::MaterialHandle material = Models::INVALID_MATERIAL;
        bool camera_layer = false;
    };

    struct DrawItem {
        std::size_t first_vertex = 0u;
        std::size_t vertex_count = 0u;
        Models::MaterialHandle material = Models::INVALID_MATERIAL;
        Ecs::Entity entity = Ecs::INVALID_ENTITY;
        bool camera_layer = false;
    };

    RasterGeometry() = default;
    ~RasterGeometry();

    RasterGeometry(const RasterGeometry&) = delete;
    RasterGeometry& operator=(const RasterGeometry&) = delete;

    bool sync(
        const Ecs::World& world,
        const Scenes::SceneCache& scene,
        std::string *error = nullptr
    );
    void bind(SDL_GPURenderPass *pass, Uint32 first_slot = 0u) const;
    void clear();

    std::size_t vertexCount() const;
    std::size_t worldVertexCount() const;
    std::size_t cameraFirstVertex() const;
    std::size_t cameraVertexCount() const;
    bool hasCameraGeometry() const;
    const std::vector<DrawRange>& draws() const;
    const std::vector<DrawItem>& drawItems() const;
    std::uint64_t revision() const;
    std::uint64_t shadowRevision() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::RasterizerSDLGPU

#endif
