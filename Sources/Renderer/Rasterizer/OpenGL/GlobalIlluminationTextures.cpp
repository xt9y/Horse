#include "Renderer/Rasterizer/OpenGL/GlobalIlluminationTextures.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <cstddef>
#include <vector>

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB 0x881A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer::RasterizerOpenGL {

GlobalIlluminationTextures::~GlobalIlluminationTextures()
{
    clear();
}

bool GlobalIlluminationTextures::bind(
    const GlobalIllumination::Field *field,
    int first_texture_unit)
{
    if (!field || !field->valid()) {
        uploaded_ = false;
        return false;
    }

    if (textures_[0] == 0u) {
        glGenTextures(4, textures_);
        for (const unsigned int texture : textures_)
            if (texture == 0u) return false;
    }

    if (!uploaded_ || revision_ != field->revision) {
        const std::size_t probe_count = field->probes.size();
        std::vector<float> data(probe_count * 4u, 0.0f);
        for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
            for (std::size_t probe = 0u; probe < probe_count; ++probe) {
                const Vec3 value = field->probes[probe].sh[coefficient];
                const std::size_t offset = probe * 4u;
                data[offset + 0u] = value.x;
                data[offset + 1u] = value.y;
                data[offset + 2u] = value.z;
            }

            GLModern.glActiveTexture(
                static_cast<GLenum>(GL_TEXTURE0 + first_texture_unit + static_cast<int>(coefficient))
            );
            glBindTexture(GL_TEXTURE_3D, textures_[coefficient]);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            GLModern.glTexImage3D(
                GL_TEXTURE_3D,
                0,
                GL_RGBA16F_ARB,
                static_cast<GLsizei>(field->size_x),
                static_cast<GLsizei>(field->size_y),
                static_cast<GLsizei>(field->size_z),
                0,
                GL_RGBA,
                GL_FLOAT,
                data.data()
            );
        }
        revision_ = field->revision;
        uploaded_ = true;
    } else {
        for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
            GLModern.glActiveTexture(
                static_cast<GLenum>(GL_TEXTURE0 + first_texture_unit + static_cast<int>(coefficient))
            );
            glBindTexture(GL_TEXTURE_3D, textures_[coefficient]);
        }
    }

    GLModern.glActiveTexture(GL_TEXTURE0);
    return true;
}

void GlobalIlluminationTextures::clear()
{
    for (unsigned int& texture : textures_) {
        if (texture != 0u) glDeleteTextures(1, &texture);
        texture = 0u;
    }
    revision_ = 0u;
    uploaded_ = false;
}

} // namespace Renderer::RasterizerOpenGL
