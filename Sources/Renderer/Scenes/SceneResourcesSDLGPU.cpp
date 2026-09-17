#include "Renderer/Internal/SceneResourcesSDLGPU.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Internal/ShadingState.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <unordered_set>

namespace Renderer::Scenes::SDLGPU {
namespace {

constexpr SDL_GPUBufferUsageFlags SceneBufferUsage =
    SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;

} // namespace

SceneResources::~SceneResources()
{
    clear();
}

bool SceneResources::init(std::string *error)
{
    if (error) error->clear();
    if (white_ && sampler_) return true;
    if (!Renderer::SDLGPU::device()) {
        if (error) *error = "SDL_GPU device is not initialized";
        return false;
    }

    const std::array<std::uint8_t, 4> white{{255u, 255u, 255u, 255u}};
    white_ = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        1u, 1u,
        "Horse White Texture");
    if (!white_ || !Renderer::SDLGPU::uploadTextureRgba8(white_, 1u, 1u, white.data(), white.size())) {
        if (error) *error = "failed to create SDL_GPU fallback texture";
        clear();
        return false;
    }
    sampler_ = Renderer::SDLGPU::createLinearSampler();
    if (!sampler_) {
        if (error) *error = "failed to create SDL_GPU scene sampler";
        clear();
        return false;
    }
    for (auto& binding : texture_bindings_) binding = {white_, sampler_};
    return true;
}

bool SceneResources::ensureBuffer(
    SDL_GPUBuffer *&target,
    std::size_t& capacity,
    SDL_GPUBufferUsageFlags usage,
    std::size_t bytes,
    const char *label)
{
    const std::size_t safe_bytes = std::max<std::size_t>(bytes, 16u);
    if (target && capacity >= safe_bytes) return true;
    SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
        usage,
        safe_bytes,
        nullptr,
        label);
    if (!replacement) return false;
    if (target) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), target);
    target = replacement;
    capacity = safe_bytes;
    return true;
}

SDL_GPUTexture *SceneResources::textureFor(Models::TextureHandle handle, std::string *error)
{
    if (const auto found = texture_cache_.find(handle); found != texture_cache_.end())
        return found->second;

    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset) {
        if (error) *error = "scene texture handle is invalid";
        return nullptr;
    }
    if (!Models::Internal::textureStorageReady(handle))
        return white_;
    if (asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
        if (error) *error = "scene texture has no RGBA image data";
        return nullptr;
    }

    SDL_GPUTexture *texture = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        static_cast<std::uint32_t>(asset->image.width),
        static_cast<std::uint32_t>(asset->image.height),
        "Horse Material Texture");
    if (!texture || !Renderer::SDLGPU::uploadTextureRgba8(
            texture,
            static_cast<std::uint32_t>(asset->image.width),
            static_cast<std::uint32_t>(asset->image.height),
            asset->image.rgba.data(),
            asset->image.rgba.size()))
    {
        if (texture) SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), texture);
        if (error) *error = "failed to upload SDL_GPU scene texture";
        return nullptr;
    }

    texture_cache_.emplace(handle, texture);
    return texture;
}

bool SceneResources::syncBuffers(bool include_geometry, std::string *error)
{
    const bool geometry_changed = include_geometry && geometry_revision_ != scene_.geometryRevision();
    const bool material_changed = material_revision_ != materials_.revision();
    if (!geometry_changed && !material_changed) return true;

    const std::size_t node_bytes = scene_.nodes().size() * sizeof(GpuNode);
    const std::size_t triangle_bytes = scene_.triangles().size() * sizeof(GpuTriangle);
    const std::size_t base_material_bytes = scene_.materials().size() * sizeof(GpuMaterial);
    const std::size_t material_bytes =
        materials_.materials().size() * sizeof(Trace::GpuAdvancedMaterial);

    if (geometry_changed &&
        (!ensureBuffer(nodes_, node_capacity_, SceneBufferUsage, node_bytes, "Horse BVH Nodes") ||
         !ensureBuffer(
             triangles_, triangle_capacity_, SceneBufferUsage, triangle_bytes,
             "Horse Scene Triangles")))
    {
        if (error) *error = "failed to allocate SDL_GPU scene geometry buffers";
        return false;
    }
    if (material_changed &&
        (!ensureBuffer(
             base_materials_, base_material_capacity_, SceneBufferUsage, base_material_bytes,
             "Horse Base Materials") ||
         !ensureBuffer(
             materials_buffer_, material_capacity_, SceneBufferUsage, material_bytes,
             "Horse Scene Materials")))
    {
        if (error) *error = "failed to allocate SDL_GPU scene material buffers";
        return false;
    }

    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
    if (!command) {
        if (error) *error = "failed to acquire SDL_GPU scene upload command buffer";
        return false;
    }

    const bool uploaded =
        (!geometry_changed ||
            ((node_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command, nodes_, scene_.nodes().data(), node_bytes, true)) &&
             (triangle_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command, triangles_, scene_.triangles().data(), triangle_bytes, true)))) &&
        (!material_changed ||
            ((base_material_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command, base_materials_, scene_.materials().data(), base_material_bytes, true)) &&
             (material_bytes == 0u || Renderer::SDLGPU::uploadBuffer(
                command, materials_buffer_, materials_.materials().data(), material_bytes, true))));

    const bool submitted = uploaded && SDL_SubmitGPUCommandBuffer(command);
    if (!submitted) {
        SDL_CancelGPUCommandBuffer(command);
        if (error) *error = "failed to upload SDL_GPU scene buffers";
        return false;
    }

    if (geometry_changed) geometry_revision_ = scene_.geometryRevision();
    if (material_changed) material_revision_ = materials_.revision();
    return true;
}

void SceneResources::clearTextures()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        for (auto& [handle, texture] : texture_cache_) {
            (void)handle;
            if (texture) SDL_ReleaseGPUTexture(device, texture);
        }
    }
    texture_cache_.clear();
    for (auto& binding : texture_bindings_) binding = {white_, sampler_};
}

void SceneResources::clearRasterMaterials()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        for (RasterMaterialResources& material : raster_materials_) {
            if (material.materials) SDL_ReleaseGPUBuffer(device, material.materials);
            if (material.base_materials) SDL_ReleaseGPUBuffer(device, material.base_materials);
        }
    }
    raster_materials_.clear();
    raster_material_indices_.clear();
    raster_material_revision_ = UINT64_MAX;
}

bool SceneResources::syncTextures(std::string *error)
{
    if (resource_revision_ == scene_.resourceRevision()) return true;
    if (materials_.textureHandles().size() > MaximumTextureSlots) {
        if (error) *error = "SDL_GPU scene texture slot capacity exceeded";
        return false;
    }

    clearTextures();
    for (std::size_t slot = 0u; slot < materials_.textureHandles().size(); ++slot) {
        SDL_GPUTexture *texture = textureFor(materials_.textureHandles()[slot], error);
        if (!texture) return false;
        texture_bindings_[slot] = {texture, sampler_};
    }
    resource_revision_ = scene_.resourceRevision();
    return true;
}

bool SceneResources::syncRasterMaterials(
    const Ecs::World& world,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (raster_material_revision_ == scene_.resourceRevision()) return true;

    clearRasterMaterials();
    maximum_texture_slots = std::min(maximum_texture_slots, MaximumRasterTextureSlots);

    std::unordered_set<Models::MaterialHandle> seen;
    for (const Scene::RenderItem& item : scene_.renderItems()) {
        if (!item.mesh_component || !item.material ||
            item.material->opacity < SceneCache::opacityCutoff())
            continue;

        const Models::MaterialHandle handle = item.mesh_component->material;
        if (handle == Models::INVALID_MATERIAL || !seen.insert(handle).second) continue;

        SceneCache material_scene;
        const std::vector<Scene::RenderItem> material_items{item};
        if (!material_scene.syncResources(world, material_items, maximum_texture_slots, error)) {
            clearRasterMaterials();
            return false;
        }

        Trace::MaterialSet material_set;
        if (!material_set.sync(material_scene, maximum_texture_slots, error)) {
            clearRasterMaterials();
            return false;
        }

        const auto& base_materials = material_scene.materials();
        const auto& advanced_materials = material_set.materials();
        if (base_materials.empty() || advanced_materials.empty()) {
            if (error) *error = "raster material resources are empty";
            clearRasterMaterials();
            return false;
        }

        RasterMaterialResources resources;
        resources.material = handle;
        resources.material_count = std::min(base_materials.size(), advanced_materials.size());
        resources.texture_count = material_set.textureHandles().size();
        for (auto& binding : resources.texture_bindings) binding = {white_, sampler_};

        resources.base_materials = Renderer::SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            base_materials.size() * sizeof(GpuMaterial),
            base_materials.data(),
            "Horse Raster Base Material");
        resources.materials = Renderer::SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            advanced_materials.size() * sizeof(Trace::GpuAdvancedMaterial),
            advanced_materials.data(),
            "Horse Raster Material");
        if (!resources.base_materials || !resources.materials) {
            if (resources.materials)
                SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), resources.materials);
            if (resources.base_materials)
                SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), resources.base_materials);
            if (error) *error = "failed to allocate SDL_GPU raster material buffers";
            clearRasterMaterials();
            return false;
        }

        const auto& texture_handles = material_set.textureHandles();
        for (std::size_t slot = 0u; slot < texture_handles.size(); ++slot) {
            SDL_GPUTexture *texture = textureFor(texture_handles[slot], error);
            if (!texture) {
                SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), resources.materials);
                SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), resources.base_materials);
                clearRasterMaterials();
                return false;
            }
            resources.texture_bindings[slot] = {texture, sampler_};
        }

        raster_material_indices_.emplace(handle, raster_materials_.size());
        raster_materials_.push_back(std::move(resources));
    }

    raster_material_revision_ = scene_.resourceRevision();
    return true;
}

SceneResources::SyncResult SceneResources::sync(const Ecs::World& world, std::string *error)
{
    return sync(world, MaximumTextureSlots, error);
}

SceneResources::SyncResult SceneResources::sync(
    const Ecs::World& world,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (!init(error)) return {};
    Scenes::Scene::collectRenderItems(world, render_items_);
    const std::uint64_t old_geometry = scene_.geometryRevision();
    const std::uint64_t old_resources = scene_.resourceRevision();
    maximum_texture_slots = std::min(maximum_texture_slots, MaximumTextureSlots);
    if (!scene_.sync(world, render_items_, maximum_texture_slots, error)) return {};
    if (!materials_.sync(scene_, maximum_texture_slots, error)) return {};
    if (!syncBuffers(true, error) || !syncTextures(error)) return {};
    return {
        true,
        old_geometry != scene_.geometryRevision() || old_resources != scene_.resourceRevision()
    };
}

SceneResources::SyncResult SceneResources::syncRaster(
    const Ecs::World& world,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (!init(error)) return {};
    Scenes::Scene::collectRenderItems(world, render_items_);
    const std::uint64_t old_resources = scene_.resourceRevision();
    maximum_texture_slots = std::min(maximum_texture_slots, MaximumTextureSlots);
    if (!scene_.syncResources(world, render_items_, maximum_texture_slots, error)) return {};
    if (!materials_.sync(scene_, maximum_texture_slots, error)) return {};
    if (!syncBuffers(false, error) || !syncTextures(error) ||
        !syncRasterMaterials(world, maximum_texture_slots, error)) return {};
    return {true, old_resources != scene_.resourceRevision()};
}

void SceneResources::bindVertex(SDL_GPURenderPass *pass) const
{
    if (!pass || !triangles_) return;
    SDL_GPUBuffer *buffers[] = {triangles_};
    SDL_BindGPUVertexStorageBuffers(pass, 0u, buffers, 1u);
}

void SceneResources::bindFragment(SDL_GPURenderPass *pass) const
{
    if (!pass) return;
    SDL_BindGPUFragmentSamplers(
        pass, 0u, texture_bindings_.data(),
        static_cast<Uint32>(texture_bindings_.size()));
    SDL_GPUBuffer *buffers[] = {nodes_, triangles_, base_materials_, materials_buffer_};
    SDL_BindGPUFragmentStorageBuffers(pass, 0u, buffers, 4u);
}

void SceneResources::bindRasterFragment(SDL_GPURenderPass *pass) const
{
    if (!pass) return;
    SDL_BindGPUFragmentSamplers(
        pass, 0u, texture_bindings_.data(),
        static_cast<Uint32>(MaximumTextureSlots - 1u));
    SDL_GPUBuffer *buffers[] = {base_materials_, materials_buffer_};
    SDL_BindGPUFragmentStorageBuffers(pass, 0u, buffers, 2u);
}

SceneResources::RasterBinding SceneResources::bindRasterMaterial(
    SDL_GPURenderPass *pass,
    Models::MaterialHandle material) const
{
    if (!pass) return {};
    const auto found = raster_material_indices_.find(material);
    if (found == raster_material_indices_.end() || found->second >= raster_materials_.size())
        return {};

    const RasterMaterialResources& resources = raster_materials_[found->second];
    if (!resources.base_materials || !resources.materials) return {};

    SDL_BindGPUFragmentSamplers(
        pass,
        0u,
        resources.texture_bindings.data(),
        static_cast<Uint32>(resources.texture_bindings.size()));
    SDL_GPUBuffer *buffers[] = {resources.base_materials, resources.materials};
    SDL_BindGPUFragmentStorageBuffers(pass, 0u, buffers, 2u);
    return {true, resources.material_count, resources.texture_count};
}

void SceneResources::bindSky(SDL_GPURenderPass *pass) const
{
    if (!pass) return;
    SDL_BindGPUFragmentSamplers(pass, 0u, texture_bindings_.data(), 1u);
}

void SceneResources::bindCompute(SDL_GPUComputePass *pass) const
{
    if (!pass) return;
    SDL_BindGPUComputeSamplers(
        pass, 0u, texture_bindings_.data(),
        static_cast<Uint32>(texture_bindings_.size()));
    SDL_GPUBuffer *buffers[] = {nodes_, triangles_, base_materials_, materials_buffer_};
    SDL_BindGPUComputeStorageBuffers(pass, 0u, buffers, 4u);
}

bool SceneResources::hasEnvironmentTexture() const
{
    const EnvironmentState environment = Internal::shadingState().environment;
    return environment.valid && environment.texture != Models::INVALID_TEXTURE &&
        !materials_.textureHandles().empty() && materials_.textureHandles().front() == environment.texture;
}

void SceneResources::clear()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    clearRasterMaterials();
    clearTextures();
    if (device) {
        if (sampler_) SDL_ReleaseGPUSampler(device, sampler_);
        if (white_) SDL_ReleaseGPUTexture(device, white_);
        if (materials_buffer_) SDL_ReleaseGPUBuffer(device, materials_buffer_);
        if (base_materials_) SDL_ReleaseGPUBuffer(device, base_materials_);
        if (triangles_) SDL_ReleaseGPUBuffer(device, triangles_);
        if (nodes_) SDL_ReleaseGPUBuffer(device, nodes_);
    }
    sampler_ = nullptr;
    white_ = nullptr;
    materials_buffer_ = nullptr;
    base_materials_ = nullptr;
    triangles_ = nullptr;
    nodes_ = nullptr;
    material_capacity_ = 0u;
    base_material_capacity_ = 0u;
    triangle_capacity_ = 0u;
    node_capacity_ = 0u;
    texture_bindings_.fill({});
    scene_.clear();
    materials_.clear();
    render_items_.clear();
    geometry_revision_ = UINT64_MAX;
    resource_revision_ = UINT64_MAX;
    material_revision_ = UINT64_MAX;
    raster_material_revision_ = UINT64_MAX;
}

} // namespace Renderer::Scenes::SDLGPU
