#include "Renderer/Systems/OpenGL/TextureCache.hpp"

#include "Models/Core/Texture.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <cstdint>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer::Systems::OpenGL {

TextureCache::~TextureCache()
{
    clear();
}

unsigned int TextureCache::white()
{
    if (white_ != 0u) return white_;

    GLModern.glActiveTexture(GL_TEXTURE0);
    GLuint texture_id = 0u;
    glGenTextures(1, &texture_id);
    if (texture_id == 0u) return 0u;

    const std::uint8_t pixel[4] = {255u, 255u, 255u, 255u};
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        1,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixel
    );
    white_ = texture_id;
    return white_;
}

unsigned int TextureCache::create(Models::TextureHandle handle)
{
    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
        return 0u;
    }

    GLModern.glActiveTexture(GL_TEXTURE0);
    GLuint texture_id = 0u;
    glGenTextures(1, &texture_id);
    if (texture_id == 0u) return 0u;

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        asset->image.width,
        asset->image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        asset->image.rgba.data()
    );
    return texture_id;
}

unsigned int TextureCache::texture(Models::TextureHandle handle)
{
    if (handle == Models::INVALID_TEXTURE) return white();
    const auto found = textures_.find(handle);
    if (found != textures_.end()) return found->second;

    const unsigned int texture_id = create(handle);
    if (texture_id != 0u) textures_.emplace(handle, texture_id);
    return texture_id;
}

void TextureCache::clear()
{
    for (const auto& [handle, texture] : textures_) {
        (void)handle;
        if (texture != 0u) {
            const GLuint id = static_cast<GLuint>(texture);
            glDeleteTextures(1, &id);
        }
    }
    textures_.clear();

    if (white_ != 0u) {
        const GLuint id = static_cast<GLuint>(white_);
        glDeleteTextures(1, &id);
        white_ = 0u;
    }
}

} // namespace Renderer::Systems::OpenGL
