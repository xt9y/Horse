#ifndef __APPLE__

#include "Renderer/Scenes/OpenGL/SceneResources.hpp"

#include "Renderer/Systems/OpenGL/TextureCache.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <limits>

#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

namespace Renderer::Scenes::OpenGL {

struct SceneResources::Impl {
    GLuint node_buffer = 0u;
    GLuint triangle_buffer = 0u;
    GLuint material_buffer = 0u;
    GLuint visibility_buffer = 0u;
    Systems::OpenGL::TextureCache textures;
    std::array<GLuint, MaximumTextureSlots> texture_slots{};
    std::uint64_t geometry_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t resource_revision = std::numeric_limits<std::uint64_t>::max();
    bool geometry_ready = false;
    bool resources_ready = false;
    bool ready = false;

    bool ensureBuffer(GLuint& buffer)
    {
        if (buffer != 0u) return true;
        GL15.glGenBuffers(1, &buffer);
        return buffer != 0u;
    }

    bool uploadBuffer(GLuint& buffer, const void *data, std::size_t bytes, GLenum usage)
    {
        if (!ensureBuffer(buffer)) return false;
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        const std::size_t safe_bytes = std::max<std::size_t>(bytes, 16u);
        GL15.glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLsizeiptr>(safe_bytes),
            bytes == 0u ? nullptr : data,
            usage
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    void clear()
    {
        if (GL15.glDeleteBuffers) {
            const GLuint buffers[] = {node_buffer, triangle_buffer, material_buffer, visibility_buffer};
            for (GLuint buffer : buffers) {
                if (buffer != 0u) GL15.glDeleteBuffers(1, &buffer);
            }
        }
        node_buffer = 0u;
        triangle_buffer = 0u;
        material_buffer = 0u;
        visibility_buffer = 0u;
        textures.clear();
        texture_slots.fill(0u);
        geometry_revision = std::numeric_limits<std::uint64_t>::max();
        resource_revision = std::numeric_limits<std::uint64_t>::max();
        geometry_ready = false;
        resources_ready = false;
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

bool SceneResources::sync(const Renderer::Scenes::SceneCache& scene, std::string *error)
{
    if (!impl_) return false;
    if (error) error->clear();

    if (scene.textureHandles().size() > MaximumTextureSlots) {
        if (error) *error = "OpenGL scene texture slot capacity exceeded";
        return false;
    }

    if (!impl_->geometry_ready || impl_->geometry_revision != scene.geometryRevision()) {
        if (
            !impl_->uploadBuffer(
                impl_->node_buffer,
                scene.nodes().data(),
                scene.nodes().size() * sizeof(GpuNode),
                GL_DYNAMIC_DRAW
            ) ||
            !impl_->uploadBuffer(
                impl_->triangle_buffer,
                scene.triangles().data(),
                scene.triangles().size() * sizeof(GpuTriangle),
                GL_DYNAMIC_DRAW
            ))
        {
            if (error) *error = "failed to upload OpenGL scene geometry buffers";
            impl_->geometry_ready = false;
            impl_->ready = false;
            return false;
        }
        impl_->geometry_revision = scene.geometryRevision();
        impl_->geometry_ready = true;
    }

    if (!impl_->resources_ready || impl_->resource_revision != scene.resourceRevision()) {
        if (!impl_->uploadBuffer(
                impl_->material_buffer,
                scene.materials().data(),
                scene.materials().size() * sizeof(GpuMaterial),
                GL_STATIC_DRAW))
        {
            if (error) *error = "failed to upload OpenGL scene material buffer";
            impl_->resources_ready = false;
            impl_->ready = false;
            return false;
        }

        impl_->texture_slots.fill(0u);
        for (std::size_t slot = 0u; slot < scene.textureHandles().size(); ++slot) {
            const GLuint texture = static_cast<GLuint>(impl_->textures.texture(scene.textureHandles()[slot]));
            if (texture == 0u) {
                if (error) *error = "failed to upload OpenGL scene texture";
                impl_->resources_ready = false;
                impl_->ready = false;
                return false;
            }
            impl_->texture_slots[slot] = texture;
        }
        impl_->resource_revision = scene.resourceRevision();
        impl_->resources_ready = true;
    }

    impl_->ready = impl_->geometry_ready && impl_->resources_ready;
    return impl_->ready;
}

bool SceneResources::syncVisibility(
    const std::vector<std::uint32_t>& visibility,
    std::string *error)
{
    if (!impl_) return false;
    std::vector<std::uint32_t> padded = visibility;
    if (padded.size() < 4u) padded.resize(4u, 0u);
    if (!impl_->uploadBuffer(
            impl_->visibility_buffer,
            padded.data(),
            padded.size() * sizeof(std::uint32_t),
            GL_DYNAMIC_DRAW))
    {
        if (error) *error = "failed to upload OpenGL visibility buffer";
        return false;
    }
    return true;
}

void SceneResources::bind()
{
    if (!impl_ || !impl_->ready) return;
    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, impl_->node_buffer);
    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, impl_->triangle_buffer);
    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, impl_->material_buffer);
    GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, impl_->visibility_buffer);
    for (std::size_t slot = 0u; slot < impl_->texture_slots.size(); ++slot) {
        GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
        glBindTexture(GL_TEXTURE_2D, impl_->texture_slots[slot]);
    }
    GLModern.glActiveTexture(GL_TEXTURE0);
}

void SceneResources::clear()
{
    if (impl_) impl_->clear();
}

bool SceneResources::ready() const
{
    return impl_ && impl_->ready;
}

} // namespace Renderer::Scenes::OpenGL

#endif
