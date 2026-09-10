#ifdef __APPLE__

#include "Renderer/Scenes/Metal/SceneResources.hpp"

#include "Models/Core/Texture.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace Renderer::Scenes::Metal {

struct SceneResources::Impl {
    LWMGLBuffer node_buffer = nullptr;
    LWMGLBuffer triangle_buffer = nullptr;
    LWMGLBuffer material_buffer = nullptr;
    LWMGLBuffer visibility_buffer = nullptr;
    std::size_t visibility_capacity = 0u;
    LWMGLTexture white_texture = nullptr;
    std::unordered_map<Models::TextureHandle, LWMGLTexture> texture_cache;
    std::array<LWMGLTexture, MaximumTextureSlots> texture_slots{};
    bool ready = false;

    bool replaceBuffer(LWMGLBuffer& target, const void *data, std::size_t bytes)
    {
        std::array<std::uint8_t, 16> zero{};
        const std::size_t safe_bytes = std::max<std::size_t>(bytes, zero.size());
        const LWMGLBufferDesc desc = {safe_bytes, LWMGL_STORAGE_SHARED};
        LWMGLBuffer replacement = ::Metal.createBuffer(&desc, bytes == 0u ? zero.data() : data);
        if (!replacement) return false;
        if (target) ::Metal.destroyBuffer(target);
        target = replacement;
        return true;
    }

    LWMGLTexture textureFor(Models::TextureHandle handle)
    {
        const auto found = texture_cache.find(handle);
        if (found != texture_cache.end()) return found->second;

        const Models::TextureAsset *asset = Models::texture(handle);
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
            return nullptr;
        }

        const LWMGLTextureDesc desc = {
            static_cast<std::uint32_t>(asset->image.width),
            static_cast<std::uint32_t>(asset->image.height),
            LWMGL_RGBA8_UNORM,
            LWMGL_TEXTURE_SAMPLED,
            LWMGL_STORAGE_SHARED
        };
        LWMGLTexture texture = ::Metal.createTexture(&desc);
        if (!texture) return nullptr;
        const std::size_t bytes_per_row = static_cast<std::size_t>(asset->image.width) * 4u;
        if (::Metal.uploadTexture2D(texture, asset->image.rgba.data(), bytes_per_row) != 0) {
            ::Metal.destroyTexture(texture);
            return nullptr;
        }
        texture_cache.emplace(handle, texture);
        return texture;
    }

    void clear()
    {
        if (node_buffer) ::Metal.destroyBuffer(node_buffer);
        if (triangle_buffer) ::Metal.destroyBuffer(triangle_buffer);
        if (material_buffer) ::Metal.destroyBuffer(material_buffer);
        if (visibility_buffer) ::Metal.destroyBuffer(visibility_buffer);
        node_buffer = nullptr;
        triangle_buffer = nullptr;
        material_buffer = nullptr;
        visibility_buffer = nullptr;
        visibility_capacity = 0u;

        for (const auto& entry : texture_cache) {
            if (entry.second) ::Metal.destroyTexture(entry.second);
        }
        texture_cache.clear();
        texture_slots.fill(nullptr);

        if (white_texture) ::Metal.destroyTexture(white_texture);
        white_texture = nullptr;
        ready = false;
    }
};

SceneResources::SceneResources() : impl_(new Impl) {}

SceneResources::~SceneResources()
{
    clear();
    delete impl_;
    impl_ = nullptr;
}

bool SceneResources::init(std::string *error)
{
    if (!impl_) return false;
    if (impl_->white_texture) return true;

    const LWMGLTextureDesc desc = {
        1u,
        1u,
        LWMGL_RGBA8_UNORM,
        LWMGL_TEXTURE_SAMPLED,
        LWMGL_STORAGE_SHARED
    };
    impl_->white_texture = ::Metal.createTexture(&desc);
    if (!impl_->white_texture) {
        if (error) *error = "failed to create Metal scene fallback texture";
        return false;
    }
    const std::uint8_t white[4] = {255u, 255u, 255u, 255u};
    if (::Metal.uploadTexture2D(impl_->white_texture, white, 4u) != 0) {
        if (error) *error = "failed to upload Metal scene fallback texture";
        ::Metal.destroyTexture(impl_->white_texture);
        impl_->white_texture = nullptr;
        return false;
    }
    impl_->texture_slots.fill(impl_->white_texture);
    return true;
}

bool SceneResources::sync(const Renderer::Scenes::SceneCache& scene, std::string *error)
{
    if (!impl_ || !init(error)) return false;
    if (scene.textureHandles().size() > MaximumTextureSlots) {
        if (error) *error = "Metal scene texture slot capacity exceeded";
        return false;
    }

    if (
        !impl_->replaceBuffer(impl_->node_buffer, scene.nodes().data(), scene.nodes().size() * sizeof(GpuNode)) ||
        !impl_->replaceBuffer(impl_->triangle_buffer, scene.triangles().data(), scene.triangles().size() * sizeof(GpuTriangle)) ||
        !impl_->replaceBuffer(impl_->material_buffer, scene.materials().data(), scene.materials().size() * sizeof(GpuMaterial)))
    {
        if (error) *error = "failed to upload Metal scene buffers";
        impl_->ready = false;
        return false;
    }

    impl_->texture_slots.fill(impl_->white_texture);
    for (std::size_t slot = 0u; slot < scene.textureHandles().size(); ++slot) {
        LWMGLTexture texture = impl_->textureFor(scene.textureHandles()[slot]);
        if (!texture) {
            if (error) *error = "failed to upload Metal scene texture";
            impl_->ready = false;
            return false;
        }
        impl_->texture_slots[slot] = texture;
    }

    impl_->ready = true;
    return true;
}

bool SceneResources::syncVisibility(
    const std::vector<std::uint32_t>& visibility,
    std::string *error)
{
    if (!impl_) return false;

    std::vector<std::uint32_t> padded = visibility;
    if (padded.size() < 4u) padded.resize(4u, 0u);
    const std::size_t bytes = padded.size() * sizeof(std::uint32_t);

    if (!impl_->visibility_buffer || impl_->visibility_capacity < padded.size()) {
        const LWMGLBufferDesc desc = {bytes, LWMGL_STORAGE_SHARED};
        LWMGLBuffer replacement = ::Metal.createBuffer(&desc, padded.data());
        if (!replacement) {
            if (error) *error = "failed to create Metal visibility buffer";
            return false;
        }
        if (impl_->visibility_buffer) ::Metal.destroyBuffer(impl_->visibility_buffer);
        impl_->visibility_buffer = replacement;
        impl_->visibility_capacity = padded.size();
        return true;
    }

    if (::Metal.uploadBuffer(impl_->visibility_buffer, 0u, padded.data(), bytes) != 0) {
        if (error) *error = "failed to upload Metal visibility buffer";
        return false;
    }
    return true;
}

bool SceneResources::bind(LWMGLCommand command, std::uint32_t first_texture_binding) const
{
    if (!impl_ || !impl_->ready || !command) return false;
    bool ok = ::Metal.setBuffer(command, impl_->node_buffer, 0u, 0u) == 0;
    if (ok) ok = ::Metal.setBuffer(command, impl_->triangle_buffer, 0u, 1u) == 0;
    if (ok) ok = ::Metal.setBuffer(command, impl_->material_buffer, 0u, 2u) == 0;
    if (ok) ok = impl_->visibility_buffer &&
        ::Metal.setBuffer(command, impl_->visibility_buffer, 0u, 6u) == 0;
    for (std::size_t slot = 0u; ok && slot < impl_->texture_slots.size(); ++slot) {
        ok = ::Metal.setTexture(
            command,
            impl_->texture_slots[slot] ? impl_->texture_slots[slot] : impl_->white_texture,
            first_texture_binding + static_cast<std::uint32_t>(slot)
        ) == 0;
    }
    return ok;
}

void SceneResources::clear()
{
    if (impl_) impl_->clear();
}

bool SceneResources::ready() const
{
    return impl_ && impl_->ready;
}

} // namespace Renderer::Scenes::Metal

#endif
