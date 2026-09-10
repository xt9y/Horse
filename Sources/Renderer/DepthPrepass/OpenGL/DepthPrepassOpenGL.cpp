#include "Renderer/DepthPrepass/OpenGL/DepthPrepassOpenGL.hpp"

#include "Models/Models.hpp"
#include "Renderer/DepthPrepass/OpenGL/DepthPrepassShaders.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/OpenGL/TextureCache.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>

namespace Renderer::DepthPrepass {
namespace {

void setInt(int location, int value)
{
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(int location, float value)
{
    if (location >= 0) GL20.glUniform1f(location, value);
}

} // namespace

struct OpenGLPass::Impl {
    Systems::OpenGL::Program program;
    int diffuse = -1;
    int has_texture = -1;
    int base_alpha = -1;
    int alpha_cutoff = -1;
    Statistics stats{};

    bool ensureProgram()
    {
        if (program.valid()) return true;
        if (lwcglLoadModernGL() != 0 || !GL20.glCreateShader || !GLModern.glActiveTexture)
            return false;
        if (!program.createGraphics(
                OpenGLShaders::vertex,
                OpenGLShaders::fragment,
                "DepthPrepass"))
        {
            return false;
        }

        diffuse = program.uniform("uDiffuse");
        has_texture = program.uniform("uHasTexture");
        base_alpha = program.uniform("uBaseAlpha");
        alpha_cutoff = program.uniform("uAlphaCutoff");
        program.use();
        setInt(diffuse, 0);
        Systems::OpenGL::unbindProgram();
        return true;
    }
};

OpenGLPass::OpenGLPass() : impl_(new Impl) {}

OpenGLPass::~OpenGLPass()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool OpenGLPass::render(const OpenGLInput& input, Systems::OpenGL::TextureCache& textures)
{
    if (!impl_ || !input.items || !impl_->ensureProgram()) return false;
    impl_->stats = {};

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(0, 0, std::max(input.width, 1), std::max(input.height, 1));
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glClear(GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(input.projection.data());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(input.view.data());

    impl_->program.use();
    setFloat(impl_->alpha_cutoff, std::clamp(input.alpha_cutoff, 0.0f, 1.0f));

    for (const Scenes::Scene::RenderItem& item : *input.items) {
        if (!item.mesh || item.mesh->indices.empty() || !item.transform) continue;

        const Models::MaterialData *material = item.material;
        const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
        if (opacity < input.alpha_cutoff) continue;

        const bool requested_texture = material && material->diffuse_texture != Models::INVALID_TEXTURE;
        const unsigned int texture = requested_texture ? textures.texture(material->diffuse_texture) : 0u;
        const bool textured = requested_texture && texture != 0u;
        const unsigned int bound = texture != 0u ? texture : textures.white();
        if (bound == 0u) continue;

        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bound);
        setInt(impl_->has_texture, textured ? 1 : 0);
        setFloat(impl_->base_alpha, opacity);

        const Math::Mat4 model = Math::modelMatrix(*item.transform);
        glPushMatrix();
        glMultMatrixf(model.data());
        glBegin(GL_TRIANGLES);
        for (const std::uint32_t index : item.mesh->indices) {
            if (index >= item.mesh->vertices.size()) continue;
            const Models::Vertex& vertex = item.mesh->vertices[index];
            glTexCoord2f(vertex.uv.x, 1.0f - vertex.uv.y);
            glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
        }
        glEnd();
        glPopMatrix();
        ++impl_->stats.drawn_items;
    }

    GLModern.glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0u);
    Systems::OpenGL::unbindProgram();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
    return true;
}

void OpenGLPass::shutdown()
{
    if (!impl_) return;
    impl_->program.destroy();
    impl_->stats = {};
}

Statistics OpenGLPass::statistics() const
{
    return impl_ ? impl_->stats : Statistics{};
}

} // namespace Renderer::DepthPrepass
