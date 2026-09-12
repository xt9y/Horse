#ifndef HORSE_RENDERER_SCENES_SCENE_RESOURCES_SDLGPU_HPP
#define HORSE_RENDERER_SCENES_SCENE_RESOURCES_SDLGPU_HPP

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

    SceneResources() = default;
    ~SceneResources();

    SceneResources(const SceneResources&) = delete;
    SceneResources& operator=(const SceneResources&) = delete;

    struct SyncResult {
        bool ok = false;
        bool scene_changed = false;
    };

    bool init(std::string *error = nullptr);
    SyncResult sync(const Ecs::World& world, std::string *error = nullptr);
    void clear();

    void bindVertex(SDL_GPURenderPass *pass) const;
    void bindFragment(SDL_GPURenderPass *pass) const;
    void bindSky(SDL_GPURenderPass *pass) const;
    void bindCompute(SDL_GPUComputePass *pass) const;

    const SceneCache& scene() const { return scene_; }
    std::size_t triangleCount() const { return scene_.triangles().size(); }
    std::size_t nodeCount() const { return scene_.nodes().size(); }
    std::size_t materialCount() const { return materials_.materials().size(); }
    std::size_t textureCount() const { return materials_.textureHandles().size(); }
    bool hasEnvironmentTexture() const;

private:
    bool syncBuffers(std::string *error);
    bool syncTextures(std::string *error);
    bool replaceBuffer(
        SDL_GPUBuffer *&target,
        SDL_GPUBufferUsageFlags usage,
        const void *data,
        std::size_t bytes,
        const char *label
    );
    SDL_GPUTexture *textureFor(Models::TextureHandle handle, std::string *error);
    void clearTextures();

    SceneCache scene_;
    Trace::MaterialSet materials_;
    std::vector<Scene::RenderItem> render_items_;

    SDL_GPUBuffer *nodes_ = nullptr;
    SDL_GPUBuffer *triangles_ = nullptr;
    SDL_GPUBuffer *base_materials_ = nullptr;
    SDL_GPUBuffer *materials_buffer_ = nullptr;
    SDL_GPUTexture *white_ = nullptr;
    SDL_GPUSampler *sampler_ = nullptr;
    std::unordered_map<Models::TextureHandle, SDL_GPUTexture *> texture_cache_;
    std::array<SDL_GPUTextureSamplerBinding, MaximumTextureSlots> texture_bindings_{};

    std::uint64_t geometry_revision_ = UINT64_MAX;
    std::uint64_t resource_revision_ = UINT64_MAX;
    std::uint64_t material_revision_ = UINT64_MAX;
};

} // namespace Renderer::Scenes::SDLGPU

#endif
