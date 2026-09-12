#ifndef __APPLE__

#include "Renderer/Trace/OpenGL/MaterialResources.hpp"

#include "Renderer/Systems/OpenGL/TextureCache.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

namespace Renderer::Trace::OpenGL {

struct MaterialResources::Impl {
    GLuint buffer = 0u;
    Systems::OpenGL::TextureCache textures;
    std::vector<GLuint> appended_textures;
    std::size_t first_texture = 0u;
    std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
    bool ready = false;
};

MaterialResources::MaterialResources() : impl_(new Impl) {}
MaterialResources::~MaterialResources() { clear(); delete impl_; }

bool MaterialResources::sync(const MaterialSet& materials, std::string *error)
{
    if (!impl_) return false;
    if (error) error->clear();
    if (impl_->ready && impl_->revision == materials.revision()) return true;
    if (!GL15.glGenBuffers || !GL15.glBindBuffer || !GL15.glBufferData || !GL30.glBindBufferBase) {
        if (error) *error = "OpenGL advanced material SSBO API unavailable";
        return false;
    }
    if (impl_->buffer == 0u) GL15.glGenBuffers(1, &impl_->buffer);
    if (impl_->buffer == 0u) {
        if (error) *error = "failed to create OpenGL advanced material buffer";
        return false;
    }

    GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, impl_->buffer);
    const std::size_t bytes = materials.materials().size() * sizeof(GpuAdvancedMaterial);
    GL15.glBufferData(
        GL_SHADER_STORAGE_BUFFER,
        static_cast<LWCGLsizeiptr>(std::max<std::size_t>(bytes, 16u)),
        bytes == 0u ? nullptr : materials.materials().data(),
        GL_DYNAMIC_DRAW
    );
    GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);

    impl_->appended_textures.clear();
    impl_->first_texture = materials.baseTextureCount();
    const auto& handles = materials.textureHandles();
    for (std::size_t index = impl_->first_texture; index < handles.size(); ++index) {
        const GLuint texture = static_cast<GLuint>(impl_->textures.texture(handles[index]));
        if (texture == 0u) {
            if (error) *error = "failed to upload OpenGL advanced material texture";
            impl_->ready = false;
            return false;
        }
        impl_->appended_textures.push_back(texture);
    }

    impl_->revision = materials.revision();
    impl_->ready = true;
    return true;
}

void MaterialResources::bind() const
{
    if (!impl_ || !impl_->ready) return;
    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7u, impl_->buffer);
    for (std::size_t index = 0u; index < impl_->appended_textures.size(); ++index) {
        GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + impl_->first_texture + index));
        glBindTexture(GL_TEXTURE_2D, impl_->appended_textures[index]);
    }
    GLModern.glActiveTexture(GL_TEXTURE0);
}

void MaterialResources::clear()
{
    if (!impl_) return;
    if (impl_->buffer != 0u && GL15.glDeleteBuffers) GL15.glDeleteBuffers(1, &impl_->buffer);
    impl_->buffer = 0u;
    impl_->textures.clear();
    impl_->appended_textures.clear();
    impl_->first_texture = 0u;
    impl_->revision = std::numeric_limits<std::uint64_t>::max();
    impl_->ready = false;
}

bool MaterialResources::ready() const { return impl_ && impl_->ready; }

} // namespace Renderer::Trace::OpenGL

#endif
