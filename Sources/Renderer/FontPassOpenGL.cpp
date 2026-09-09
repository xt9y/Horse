#include "Renderer/FontPass.hpp"

#include "Models/Images/Image.hpp"
#include "Renderer/FontAtlas.hpp"

#include <lwcgl/lwcgl.h>

#include <cstddef>
#include <string>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer::Internal {
namespace {

GLuint font_texture = 0u;

GLuint fontTexture()
{
    if (font_texture != 0u) return font_texture;

    Models::Images::Image image;
    std::string error;
    const bool loaded = Models::Images::load(FontAtlas::ASSET_PATH, &image, &error)
        && image.width == FontAtlas::WIDTH
        && image.height == FontAtlas::HEIGHT
        && image.rgba.size() == static_cast<std::size_t>(FontAtlas::WIDTH * FontAtlas::HEIGHT * 4);

    if (!loaded) {
        image.width = FontAtlas::WIDTH;
        image.height = FontAtlas::HEIGHT;
        image.rgba = FontAtlas::rgba();
        image.meaningful_alpha = true;
    }

    glGenTextures(1, &font_texture);
    if (font_texture == 0u) return 0u;

    glBindTexture(GL_TEXTURE_2D, font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        image.width,
        image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.rgba.data()
    );
    return font_texture;
}

void drawBatch(const std::vector<FontVertex>& vertices, bool depth_test)
{
    if (vertices.empty()) return;

    if (depth_test) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);

    glBegin(GL_TRIANGLES);
    for (const FontVertex& vertex : vertices) {
        glColor4f(
            vertex.color[0],
            vertex.color[1],
            vertex.color[2],
            vertex.color[3]
        );
        glTexCoord2f(vertex.uv_depth[0], 1.0f - vertex.uv_depth[1]);
        glVertex4f(
            vertex.clip[0],
            vertex.clip[1],
            vertex.clip[2],
            vertex.clip[3]
        );
    }
    glEnd();
}

} // namespace

void renderFontsOpenGL(const FontBatches& batches, FrameOutput& output)
{
    (void)output;
    const GLuint texture = fontTexture();
    if (texture == 0u) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    drawBatch(batches.depth, true);
    drawBatch(batches.overlay, false);

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glPopAttrib();
}

void shutdownFontsOpenGL()
{
    if (font_texture == 0u) return;
    glDeleteTextures(1, &font_texture);
    font_texture = 0u;
}

} // namespace Renderer::Internal
