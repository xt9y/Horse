#include "Renderer/FontPass.hpp"

#include "Models/Images/Image.hpp"
#include "Renderer/FontAtlas.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer::Internal {
namespace {

GLuint font_texture = 0u;
GLuint linear_depth_program = 0u;

GLuint compileShader(GLenum stage, const char *source)
{
    const GLuint shader = GL20.glCreateShader(stage);
    if (shader == 0u) return 0u;

    GL20.glShaderSource(shader, 1, &source, nullptr);
    GL20.glCompileShader(shader);

    GLint status = 0;
    GL20.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return shader;

    GLint length = 0;
    GL20.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetShaderInfoLog(shader, length, nullptr, log.data());
    std::fprintf(stderr, "[Font]: OpenGL shader compile failed: %s\n", log.data());
    GL20.glDeleteShader(shader);
    return 0u;
}

GLuint linearDepthProgram()
{
    if (linear_depth_program != 0u) return linear_depth_program;
    if (!GL20.glCreateShader || !GL20.glCreateProgram || !GL20.glGetUniformLocation) return 0u;

    static constexpr const char *vertex_source = R"GLSL(
#version 430 compatibility
out vec2 vUv;
out float vLinearDepth;
out float vDepthTest;
out vec4 vColor;
void main()
{
    gl_Position = gl_Vertex;
    vUv = gl_MultiTexCoord0.xy;
    vLinearDepth = gl_MultiTexCoord0.z;
    vDepthTest = gl_MultiTexCoord0.w;
    vColor = gl_Color;
}
)GLSL";

    static constexpr const char *fragment_source = R"GLSL(
#version 430 compatibility
uniform sampler2D uAtlas;
uniform sampler2D uSceneDepth;
uniform vec2 uOutputSize;
in vec2 vUv;
in float vLinearDepth;
in float vDepthTest;
in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main()
{
    vec4 texel = texture(uAtlas, vUv);
    if (texel.a * vColor.a < 0.5) discard;

    if (vDepthTest > 0.5) {
        vec2 output_size = max(uOutputSize, vec2(1.0));
        vec2 screen_uv = clamp(gl_FragCoord.xy / output_size, vec2(0.0), vec2(0.999999));
        ivec2 depth_size = textureSize(uSceneDepth, 0);
        ivec2 pixel = min(ivec2(screen_uv * vec2(depth_size)), depth_size - ivec2(1));
        float surface_depth = texelFetch(uSceneDepth, pixel, 0).r;
        float epsilon = max(0.0025, surface_depth * 0.0005);
        if (vLinearDepth > surface_depth + epsilon) discard;
    }

    outColor = vec4(vColor.rgb, texel.a * vColor.a);
}
)GLSL";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertex_source);
    if (vertex == 0u) return 0u;
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment == 0u) {
        GL20.glDeleteShader(vertex);
        return 0u;
    }

    const GLuint program = GL20.glCreateProgram();
    if (program == 0u) {
        GL20.glDeleteShader(vertex);
        GL20.glDeleteShader(fragment);
        return 0u;
    }

    GL20.glAttachShader(program, vertex);
    GL20.glAttachShader(program, fragment);
    GL20.glLinkProgram(program);
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);

    GLint status = 0;
    GL20.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint length = 0;
        GL20.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        GL20.glGetProgramInfoLog(program, length, nullptr, log.data());
        std::fprintf(stderr, "[Font]: OpenGL program link failed: %s\n", log.data());
        GL20.glDeleteProgram(program);
        return 0u;
    }

    linear_depth_program = program;
    GL20.glUseProgram(linear_depth_program);
    const GLint atlas = GL20.glGetUniformLocation(linear_depth_program, "uAtlas");
    const GLint scene_depth = GL20.glGetUniformLocation(linear_depth_program, "uSceneDepth");
    if (atlas >= 0) GL20.glUniform1i(atlas, 0);
    if (scene_depth >= 0) GL20.glUniform1i(scene_depth, 1);
    GL20.glUseProgram(0u);
    return linear_depth_program;
}

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

void drawFixedBatch(const std::vector<FontVertex>& vertices, bool depth_test)
{
    if (vertices.empty()) return;

    if (depth_test) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);

    glBegin(GL_TRIANGLES);
    for (const FontVertex& vertex : vertices) {
        glColor4f(vertex.color[0], vertex.color[1], vertex.color[2], vertex.color[3]);
        glTexCoord2f(vertex.uv_depth[0], vertex.uv_depth[1]);
        glVertex4f(vertex.clip[0], vertex.clip[1], vertex.clip[2], vertex.clip[3]);
    }
    glEnd();
}

void drawLinearBatch(const std::vector<FontVertex>& vertices)
{
    if (vertices.empty()) return;

    glBegin(GL_TRIANGLES);
    for (const FontVertex& vertex : vertices) {
        glColor4f(vertex.color[0], vertex.color[1], vertex.color[2], vertex.color[3]);
        glTexCoord4f(
            vertex.uv_depth[0],
            vertex.uv_depth[1],
            vertex.uv_depth[2],
            vertex.uv_depth[3]
        );
        glVertex4f(vertex.clip[0], vertex.clip[1], vertex.clip[2], vertex.clip[3]);
    }
    glEnd();
}

} // namespace

void renderFontsOpenGL(const FontBatches& batches, FrameOutput& output)
{
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
    glEnable(GL_TEXTURE_2D);

    const bool has_linear_depth =
        output.depth == DepthSource::LinearTexture && output.depth_texture != nullptr;

    if (has_linear_depth) {
        const GLuint program = linearDepthProgram();
        if (program != 0u && GLModern.glActiveTexture) {
            const GLuint scene_depth = static_cast<GLuint>(
                reinterpret_cast<std::uintptr_t>(output.depth_texture)
            );

            glDisable(GL_DEPTH_TEST);
            GL20.glUseProgram(program);
            const GLint output_size = GL20.glGetUniformLocation(program, "uOutputSize");
            if (output_size >= 0) {
                GL20.glUniform2f(
                    output_size,
                    static_cast<float>(std::max(output.width, 1)),
                    static_cast<float>(std::max(output.height, 1))
                );
            }

            GLModern.glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            GLModern.glActiveTexture(GL_TEXTURE0 + 1u);
            glBindTexture(GL_TEXTURE_2D, scene_depth);
            GLModern.glActiveTexture(GL_TEXTURE0);

            drawLinearBatch(batches.depth);
            drawLinearBatch(batches.overlay);

            GL20.glUseProgram(0u);
            GLModern.glActiveTexture(GL_TEXTURE0 + 1u);
            glBindTexture(GL_TEXTURE_2D, 0u);
            GLModern.glActiveTexture(GL_TEXTURE0);
        } else {
            glBindTexture(GL_TEXTURE_2D, texture);
            drawFixedBatch(batches.overlay, false);
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        drawFixedBatch(batches.depth, output.depth == DepthSource::Native);
        drawFixedBatch(batches.overlay, false);
    }

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
    if (font_texture != 0u) {
        glDeleteTextures(1, &font_texture);
        font_texture = 0u;
    }
    if (linear_depth_program != 0u && GL20.glDeleteProgram) {
        GL20.glDeleteProgram(linear_depth_program);
        linear_depth_program = 0u;
    }
}

} // namespace Renderer::Internal
