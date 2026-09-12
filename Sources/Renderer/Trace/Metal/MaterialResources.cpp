#ifdef __APPLE__

#include "Renderer/Trace/Metal/MaterialResources.hpp"

#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Renderer::Trace::Metal {

struct MaterialResources::Impl {
    LWMGLBuffer buffer = nullptr;
    std::unordered_map<Models::TextureHandle, LWMGLTexture> texture_cache;
    std::vector<LWMGLTexture> appended_textures;
    std::size_t first_texture = 0u;
    std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
    bool ready = false;

    LWMGLTexture textureFor(Models::TextureHandle handle)
    {
        const auto found = texture_cache.find(handle);
        if (found != texture_cache.end()) return found->second;
        const Models::TextureAsset *asset = Models::texture(handle);
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty())
            return nullptr;
        const LWMGLTextureDesc desc = {
            static_cast<std::uint32_t>(asset->image.width),
            static_cast<std::uint32_t>(asset->image.height),
            LWMGL_RGBA8_UNORM,
            LWMGL_TEXTURE_SAMPLED,
            LWMGL_STORAGE_SHARED,
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
};

MaterialResources::MaterialResources() : impl_(new Impl) {}
MaterialResources::~MaterialResources() { clear(); delete impl_; }

bool MaterialResources::sync(const MaterialSet& materials, std::string *error)
{
    if (!impl_) return false;
    if (error) error->clear();
    if (impl_->ready && impl_->revision == materials.revision()) return true;

    const std::size_t bytes = materials.materials().size() * sizeof(GpuAdvancedMaterial);
    const std::size_t safe_bytes = std::max<std::size_t>(bytes, 16u);
    if (!impl_->buffer) {
        const LWMGLBufferDesc desc = {safe_bytes, LWMGL_STORAGE_SHARED};
        impl_->buffer = ::Metal.createBuffer(&desc, bytes ? materials.materials().data() : nullptr);
        if (!impl_->buffer) {
            if (error) *error = "failed to create Metal advanced material buffer";
            return false;
        }
    } else {
        ::Metal.destroyBuffer(impl_->buffer);
        impl_->buffer = nullptr;
        const LWMGLBufferDesc desc = {safe_bytes, LWMGL_STORAGE_SHARED};
        impl_->buffer = ::Metal.createBuffer(&desc, bytes ? materials.materials().data() : nullptr);
        if (!impl_->buffer) {
            if (error) *error = "failed to replace Metal advanced material buffer";
            return false;
        }
    }

    impl_->first_texture = materials.baseTextureCount();
    impl_->appended_textures.clear();
    const auto& handles = materials.textureHandles();
    for (std::size_t index = impl_->first_texture; index < handles.size(); ++index) {
        LWMGLTexture texture = impl_->textureFor(handles[index]);
        if (!texture) {
            if (error) *error = "failed to upload Metal advanced material texture";
            impl_->ready = false;
            return false;
        }
        impl_->appended_textures.push_back(texture);
    }

    impl_->revision = materials.revision();
    impl_->ready = true;
    return true;
}

bool MaterialResources::bind(LWMGLCommand command, std::uint32_t first_texture_binding) const
{
    if (!impl_ || !impl_->ready || !command) return false;
    bool ok = ::Metal.setBuffer(command, impl_->buffer, 0u, 7u) == 0;
    for (std::size_t index = 0u; ok && index < impl_->appended_textures.size(); ++index) {
        ok = ::Metal.setTexture(
            command,
            impl_->appended_textures[index],
            first_texture_binding + static_cast<std::uint32_t>(impl_->first_texture + index)
        ) == 0;
    }
    return ok;
}

void MaterialResources::clear()
{
    if (!impl_) return;
    if (::Metal.isCreated()) {
        if (impl_->buffer) ::Metal.destroyBuffer(impl_->buffer);
        for (const auto& entry : impl_->texture_cache)
            if (entry.second) ::Metal.destroyTexture(entry.second);
    }
    impl_->buffer = nullptr;
    impl_->texture_cache.clear();
    impl_->appended_textures.clear();
    impl_->first_texture = 0u;
    impl_->revision = std::numeric_limits<std::uint64_t>::max();
    impl_->ready = false;
}

bool MaterialResources::ready() const { return impl_ && impl_->ready; }

} // namespace Renderer::Trace::Metal

#endif
