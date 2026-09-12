#include "Renderer/Scenes/SceneResourcesSDLGPU.hpp"

#include "Models/Core/Texture.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/ShadingState.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

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

bool SceneResources::replaceBuffer(
    SDL_GPUBuffer *&target,
    SDL_GPUBufferUsageFlags usage,
    const void *data,
    std::size_t bytes,
    const char *label)
{
    std::array<std::uint8_t, 16> zero{};
    const std::size_t safe_bytes = std::max(bytes, zero.size());
    SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
        usage,
        safe_bytes,
        bytes == 0u ? zero.data() : data,
        label);
    if (!replacement) return false;
    if (target) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), target);
    target = replacement;
    return true;
}

SDL_GPUTexture *SceneResources::textureFor(Models::TextureHandle handle, std::string *error)
{
    if (const auto found = texture_cache_.find(handle); found != texture_cache_.end())
        return found->second;

    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
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

bool SceneResources::syncBuffers(std::string *error)
{
    if (geometry_revision_ != scene_.geometryRevision()) {
        if (!replaceBuffer(
                nodes_, SceneBufferUsage,
                scene_.nodes().data(), scene_.nodes().size() * sizeof(GpuNode),
                "Horse BVH Nodes") ||
            !replaceBuffer(
                triangles_, SceneBufferUsage,
                scene_.triangles().data(), scene_.triangles().size() * sizeof(GpuTriangle),
                "Horse Scene Triangles"))
        {
            if (error) *error = "failed to upload SDL_GPU scene geometry";
            return false;
        }
        geometry_revision_ = scene_.geometryRevision();
    }

    if (material_revision_ != materials_.revision()) {
        if (!replaceBuffer(
                base_materials_, SceneBufferUsage,
                scene_.materials().data(),
                scene_.materials().size() * sizeof(GpuMaterial),
                "Horse Base Materials") ||
            !replaceBuffer(
                materials_buffer_, SceneBufferUsage,
                materials_.materials().data(),
                materials_.materials().size() * sizeof(Trace::GpuAdvancedMaterial),
                "Horse Scene Materials"))
        {
            if (error) *error = "failed to upload SDL_GPU scene materials";
            return false;
        }
        material_revision_ = materials_.revision();
    }
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

SceneResources::SyncResult SceneResources::sync(const Ecs::World& world, std::string *error)
{
    if (!init(error)) return {};
    Scenes::Scene::collectRenderItems(world, render_items_);
    const std::uint64_t old_geometry = scene_.geometryRevision();
    const std::uint64_t old_resources = scene_.resourceRevision();
    if (!scene_.sync(world, render_items_, MaximumTextureSlots, error)) return {};
    if (!materials_.sync(scene_, MaximumTextureSlots, error)) return {};
    if (!syncBuffers(error) || !syncTextures(error)) return {};
    return {
        true,
        old_geometry != scene_.geometryRevision() || old_resources != scene_.resourceRevision()
    };
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
    texture_bindings_.fill({});
    scene_.clear();
    materials_.clear();
    render_items_.clear();
    geometry_revision_ = UINT64_MAX;
    resource_revision_ = UINT64_MAX;
    material_revision_ = UINT64_MAX;
}

} // namespace Renderer::Scenes::SDLGPU
