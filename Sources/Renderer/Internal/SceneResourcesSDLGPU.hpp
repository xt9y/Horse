#ifndef HORSE_RENDERER_INTERNAL_SCENE_RESOURCES_SDLGPU_HPP
#define HORSE_RENDERER_INTERNAL_SCENE_RESOURCES_SDLGPU_HPP

#include "Renderer/Scenes/SceneCache.hpp"
#include "Renderer/Trace/MaterialSet.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer::Scenes::SDLGPU {

class SceneResources {
public:
    static constexpr std::size_t MaximumTextureSlots = 16u;
    static constexpr std::size_t MaximumRasterTextureSlots = MaximumTextureSlots - 1u;

    SceneResources() = default;
    ~SceneResources();

    SceneResources(const SceneResources&) = delete;
    SceneResources& operator=(const SceneResources&) = delete;

    struct SyncResult {
        bool ok = false;
        bool scene_changed = false;
    };

    struct RasterBinding {
        bool valid = false;
        std::size_t material_count = 0u;
        std::size_t texture_count = 0u;
    };

    bool init(std::string *error = nullptr);
    SyncResult sync(const Ecs::World& world, std::string *error = nullptr);
    SyncResult sync(
        const Ecs::World& world,
        std::size_t maximum_texture_slots,
        std::string *error = nullptr
    );
    SyncResult syncRaster(
        const Ecs::World& world,
        std::size_t maximum_texture_slots,
        std::string *error = nullptr
    );
    void clear();

    void bindVertex(SDL_GPURenderPass *pass) const;
    void bindFragment(SDL_GPURenderPass *pass) const;
    void bindRasterFragment(SDL_GPURenderPass *pass) const;
    RasterBinding bindRasterMaterial(
        SDL_GPURenderPass *pass,
        Models::MaterialHandle material
    ) const;
    void bindSky(SDL_GPURenderPass *pass) const;
    void bindCompute(SDL_GPUComputePass *pass) const;

    const SceneCache& scene() const { return scene_; }
    std::size_t triangleCount() const { return scene_.triangles().size(); }
    std::size_t nodeCount() const { return scene_.nodes().size(); }
    std::size_t materialCount() const { return materials_.materials().size(); }
    std::size_t textureCount() const { return materials_.textureHandles().size(); }
    SDL_GPUBuffer *nodeBuffer() const { return nodes_; }
    SDL_GPUBuffer *triangleBuffer() const { return triangles_; }
    bool hasEnvironmentTexture() const;

private:
    struct RasterMaterialResources {
        Models::MaterialHandle material = Models::INVALID_MATERIAL;
        SDL_GPUBuffer *base_materials = nullptr;
        SDL_GPUBuffer *materials = nullptr;
        std::array<SDL_GPUTextureSamplerBinding, MaximumRasterTextureSlots> texture_bindings{};
        std::size_t material_count = 0u;
        std::size_t texture_count = 0u;
    };

    bool syncBuffers(bool include_geometry, std::string *error);
    bool syncTextures(std::string *error);
    bool syncRasterMaterials(
        const Ecs::World& world,
        std::size_t maximum_texture_slots,
        std::string *error
    );
    bool ensureBuffer(
        SDL_GPUBuffer *&target,
        std::size_t& capacity,
        SDL_GPUBufferUsageFlags usage,
        std::size_t bytes,
        const char *label
    );
    SDL_GPUTexture *textureFor(
        Models::TextureHandle handle,
        SDL_GPUCommandBuffer *upload_command,
        std::string *error
    );
    void clearTextures();
    void clearRasterMaterials();

    SceneCache scene_;
    Trace::MaterialSet materials_;
    std::vector<Scene::RenderItem> render_items_;

    SDL_GPUBuffer *nodes_ = nullptr;
    SDL_GPUBuffer *triangles_ = nullptr;
    SDL_GPUBuffer *base_materials_ = nullptr;
    SDL_GPUBuffer *materials_buffer_ = nullptr;
    std::size_t node_capacity_ = 0u;
    std::size_t triangle_capacity_ = 0u;
    std::size_t base_material_capacity_ = 0u;
    std::size_t material_capacity_ = 0u;
    SDL_GPUTexture *white_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    std::unordered_map<Models::TextureHandle, SDL_GPUTexture *> texture_cache_;
    std::array<SDL_GPUTextureSamplerBinding, MaximumTextureSlots> texture_bindings_{};
    std::vector<RasterMaterialResources> raster_materials_;
    std::unordered_map<Models::MaterialHandle, std::size_t> raster_material_indices_;

    std::uint64_t texture_storage_generation_ = 0u;
    std::uint64_t geometry_revision_ = UINT64_MAX;
    std::uint64_t resource_revision_ = UINT64_MAX;
    std::uint64_t material_revision_ = UINT64_MAX;
    std::uint64_t raster_material_revision_ = UINT64_MAX;
};

} // namespace Renderer::Scenes::SDLGPU

#endif
