#ifdef __APPLE__

#include "Renderer/Trace/Metal/TextureSet.hpp"

#include <lwcgl/lwcgl.h>

namespace Renderer::Trace::Metal {

TextureSet::~TextureSet()
{
    clear();
}

bool TextureSet::create(
    int width,
    int height,
    std::initializer_list<Descriptor> descriptors)
{
    clear();
    if (width <= 0 || height <= 0 || descriptors.size() == 0u) return false;

    textures_.reserve(descriptors.size());
    for (const Descriptor& descriptor : descriptors) {
        const LWMGLTextureDesc texture_descriptor = {
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            descriptor.format,
            descriptor.usage,
            descriptor.storage
        };
        LWMGLTexture texture = ::Metal.createTexture(&texture_descriptor);
        if (!texture) {
            clear();
            return false;
        }
        textures_.push_back(texture);
    }
    return true;
}

void TextureSet::clear()
{
    for (LWMGLTexture texture : textures_) {
        if (texture) ::Metal.destroyTexture(texture);
    }
    textures_.clear();
}

LWMGLTexture TextureSet::texture(std::size_t index) const
{
    return index < textures_.size() ? textures_[index] : nullptr;
}

} // namespace Renderer::Trace::Metal

#endif
