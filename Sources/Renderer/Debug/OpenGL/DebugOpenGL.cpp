#include "Renderer/Debug/Internal.hpp"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <cstddef>

namespace Renderer::Debug::Internal {
namespace {

void drawLines(const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return;

    glVertexPointer(4, GL_FLOAT, sizeof(Vertex), vertices.data()->position.data());
    glColorPointer(4, GL_FLOAT, sizeof(Vertex), vertices.data()->color.data());
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
}

} // namespace

void renderOpenGL(
    const std::vector<Vertex>& wireframe,
    std::uint64_t wireframe_revision,
    const std::vector<Vertex>& dynamic,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output)
{
    (void)wireframe_revision;
    (void)output;
    if (wireframe.empty() && dynamic.empty()) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

    glUseProgram(0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glLineWidth(1.0f);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(projection.data());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(view.data());

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    drawLines(wireframe);
    drawLines(dynamic);

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
