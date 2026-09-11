#ifndef HORSE_RENDERER_TRACE_METAL_TEXTURE_SET_HPP
#define HORSE_RENDERER_TRACE_METAL_TEXTURE_SET_HPP

#ifdef __APPLE__

#include <lwmgl/lwmgl.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace Renderer::Trace::Metal {

class TextureSet {
public:
    struct Descriptor {
        LWMGLPixelFormat format = LWMGL_RGBA16_FLOAT;
        std::uint32_t usage = LWMGL_TEXTURE_SAMPLED;
        LWMGLStorageMode storage = LWMGL_STORAGE_PRIVATE;
    };

    TextureSet() = default;
    ~TextureSet();

    TextureSet(const TextureSet&) = delete;
    TextureSet& operator=(const TextureSet&) = delete;

    bool create(int width, int height, std::initializer_list<Descriptor> descriptors);
    void clear();

    LWMGLTexture texture(std::size_t index) const;
    std::size_t count() const { return textures_.size(); }

private:
    std::vector<LWMGLTexture> textures_;
};

} // namespace Renderer::Trace::Metal

#endif
#endif
