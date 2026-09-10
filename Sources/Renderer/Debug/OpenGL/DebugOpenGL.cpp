#include "Renderer/Debug/Internal.hpp"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

namespace Renderer::Debug::Internal {

void renderOpenGL(
    const std::vector<Vertex>& lines,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output)
{
    if (lines.empty()) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

    glUseProgram(0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (output.depth == Renderer::Internal::DepthSource::Native) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_FALSE);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(projection.data());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(view.data());

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(4, GL_FLOAT, sizeof(Vertex), lines.data()->position.data());
    glColorPointer(4, GL_FLOAT, sizeof(Vertex), lines.data()->color.data());
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glPopClientAttrib();
    glPopAttrib();
}

void shutdownOpenGL()
{
}

} // namespace Renderer::Debug::Internal
