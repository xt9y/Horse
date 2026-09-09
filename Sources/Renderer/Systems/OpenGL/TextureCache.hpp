#ifndef RW_ENGINE_RENDERER_SYSTEMS_OPENGL_TEXTURE_CACHE_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_OPENGL_TEXTURE_CACHE_HPP

#include "Models/Models.hpp"

#include <unordered_map>

namespace Renderer::Systems::OpenGL {

class TextureCache {
public:
    TextureCache() = default;
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    unsigned int texture(Models::TextureHandle handle);
    unsigned int white();
    void clear();

private:
    unsigned int create(Models::TextureHandle handle);

    std::unordered_map<Models::TextureHandle, unsigned int> textures_;
    unsigned int white_ = 0u;
};

} // namespace Renderer::Systems::OpenGL

#endif
