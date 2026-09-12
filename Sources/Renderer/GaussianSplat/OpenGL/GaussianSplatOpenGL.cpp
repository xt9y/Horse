#include "Renderer/GaussianSplat/OpenGL/GaussianSplatOpenGL.hpp"

#include "Models/GaussianSplat.hpp"
#include "Models/Models.hpp"
#include "Renderer/GaussianSplat/Projection.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"

#include <lwcgl/glmodern.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT 0x8D40
#endif
#ifndef GL_FRAMEBUFFER_BINDING_EXT
#define GL_FRAMEBUFFER_BINDING_EXT 0x8CA6
#endif
#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT_EXT
#define GL_DEPTH_ATTACHMENT_EXT 0x8D00
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_COMPLETE_EXT 0x8CD5
#endif
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#endif
#ifndef GL_TEXTURE_HEIGHT
#define GL_TEXTURE_HEIGHT 0x1001
#endif

namespace Renderer::GaussianSplat::OpenGL {
namespace {

constexpr float AlphaThreshold = 1.0f / 255.0f;

inline constexpr const char *VertexShader = R"GLSL(
#version 120
varying vec2 vKernel;
varying vec4 vColorOpacity;
varying float vLinearDepth;
void main()
{
    gl_Position = gl_Vertex;
    vKernel = gl_MultiTexCoord0.xy;
    vColorOpacity = gl_Color;
    vLinearDepth = gl_Normal.x;
}
)GLSL";

inline constexpr const char *FragmentShader = R"GLSL(
#version 120
uniform sampler2D uSceneDepth;
uniform int uLinearDepth;
uniform vec2 uInverseSize;
uniform float uAlphaThreshold;
varying vec2 vKernel;
varying vec4 vColorOpacity;
varying float vLinearDepth;
void main()
{
    float radius_squared = dot(vKernel, vKernel);
    if (radius_squared > 9.0) discard;
    if (uLinearDepth != 0) {
        float scene_depth = texture2D(uSceneDepth, gl_FragCoord.xy * uInverseSize).r;
        float epsilon = max(0.0025, scene_depth * 0.0005);
        if (vLinearDepth > scene_depth + epsilon) discard;
    }
    float alpha = clamp(vColorOpacity.a * exp(-0.5 * radius_squared), 0.0, 1.0);
    if (alpha < uAlphaThreshold) discard;
    vec3 color = max(vColorOpacity.rgb, vec3(0.0));
    gl_FragColor = vec4(color * alpha, alpha);
}
)GLSL";

using GenFramebuffersProc = void (*)(GLsizei, GLuint *);
using DeleteFramebuffersProc = void (*)(GLsizei, const GLuint *);
using BindFramebufferProc = void (*)(GLenum, GLuint);
using FramebufferTexture2DProc = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusProc = GLenum (*)(GLenum);

struct CacheEntry {
    Models::GaussianSplat::Data data;
};

struct State {
    Systems::OpenGL::Program program;
    GenFramebuffersProc gen_framebuffers = nullptr;
    DeleteFramebuffersProc delete_framebuffers = nullptr;
    BindFramebufferProc bind_framebuffer = nullptr;
    FramebufferTexture2DProc framebuffer_texture_2d = nullptr;
    CheckFramebufferStatusProc check_framebuffer = nullptr;
    GLuint framebuffer = 0u;
    int scene_depth_uniform = -1;
    int linear_depth_uniform = -1;
    int inverse_size_uniform = -1;
    int alpha_threshold_uniform = -1;
    std::uint64_t resource_revision = 0u;
    std::unordered_map<Models::MeshHandle, CacheEntry> cache;
    bool initialized = false;
};

State& state()
{
    static State value;
    return value;
}

GLFWglproc resolve(const char *core, const char *extension)
{
    GLFWglproc proc = glfwGetProcAddress(core);
    if (!proc && extension) proc = glfwGetProcAddress(extension);
    return proc;
}

bool initialize()
{
    State& value = state();
    if (value.initialized) return true;
    if (!GL20.glUniform1i || !GL20.glUniform1f || !GL20.glUniform2f) return false;
    value.gen_framebuffers = reinterpret_cast<GenFramebuffersProc>(resolve("glGenFramebuffers", "glGenFramebuffersEXT"));
    value.delete_framebuffers = reinterpret_cast<DeleteFramebuffersProc>(resolve("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    value.bind_framebuffer = reinterpret_cast<BindFramebufferProc>(resolve("glBindFramebuffer", "glBindFramebufferEXT"));
    value.framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DProc>(resolve("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    value.check_framebuffer = reinterpret_cast<CheckFramebufferStatusProc>(resolve("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
    if (!value.gen_framebuffers || !value.delete_framebuffers || !value.bind_framebuffer ||
        !value.framebuffer_texture_2d || !value.check_framebuffer)
        return false;
    if (!value.program.createGraphics(VertexShader, FragmentShader, "GaussianSplat")) return false;
    value.scene_depth_uniform = value.program.uniform("uSceneDepth");
    value.linear_depth_uniform = value.program.uniform("uLinearDepth");
    value.inverse_size_uniform = value.program.uniform("uInverseSize");
    value.alpha_threshold_uniform = value.program.uniform("uAlphaThreshold");
    value.gen_framebuffers(1, &value.framebuffer);
    if (value.framebuffer == 0u) {
        value.program.destroy();
        return false;
    }
    value.initialized = true;
    return true;
}

bool textureSize(GLuint texture, int *width, int *height)
{
    if (!width || !height || texture == 0u) return false;
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint w = 0;
    GLint h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (w <= 0 || h <= 0) return false;
    *width = w;
    *height = h;
    return true;
}

bool decoded(const Systems::Scene::RenderItem& item, const Models::GaussianSplat::Data **output, std::string *error)
{
    if (!output || !item.mesh || !item.mesh_component) return false;
    State& value = state();
    const Models::MeshHandle handle = item.mesh_component->mesh;
    auto found = value.cache.find(handle);
    if (found == value.cache.end()) {
        CacheEntry entry;
        if (!Models::GaussianSplat::decode(*item.mesh, &entry.data, error)) return false;
        found = value.cache.emplace(handle, std::move(entry)).first;
    }
    *output = &found->second.data;
    return true;
}

void emitVertex(const ProjectedSplat& splat, float x, float y)
{
    const float px = splat.center_x + splat.axis0_x * x + splat.axis1_x * y;
    const float py = splat.center_y + splat.axis0_y * x + splat.axis1_y * y;
    glNormal3f(splat.linear_depth, 0.0f, 0.0f);
    glTexCoord2f(x, y);
    glColor4f(splat.color.x, splat.color.y, splat.color.z, splat.opacity);
    glVertex4f(px, py, splat.ndc_depth, 1.0f);
}

void drawSplat(const ProjectedSplat& splat)
{
    emitVertex(splat, -3.0f, -3.0f);
    emitVertex(splat,  3.0f, -3.0f);
    emitVertex(splat,  3.0f,  3.0f);
    emitVertex(splat, -3.0f, -3.0f);
    emitVertex(splat,  3.0f,  3.0f);
    emitVertex(splat, -3.0f,  3.0f);
}

bool attachFrame(Internal::FrameOutput& output, int *width, int *height, GLint *previous_framebuffer)
{
    State& value = state();
    const GLuint color = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(output.color_texture));
    const GLuint depth = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(output.depth_texture));
    if (color == 0u || !textureSize(color, width, height) || !previous_framebuffer) return false;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, previous_framebuffer);
    value.bind_framebuffer(GL_FRAMEBUFFER_EXT, value.framebuffer);
    value.framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, color, 0);
    value.framebuffer_texture_2d(
        GL_FRAMEBUFFER_EXT,
        GL_DEPTH_ATTACHMENT_EXT,
        GL_TEXTURE_2D,
        output.depth == Internal::DepthSource::Native ? depth : 0u,
        0
    );
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    if (value.check_framebuffer(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
        value.bind_framebuffer(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(*previous_framebuffer));
        return false;
    }
    return true;
}

} // namespace

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    std::vector<Systems::Scene::RenderItem> items;
    Systems::Scene::collectGaussianItems(world, items);
    if (items.empty() || !output.color_texture) return true;
    if (!initialize()) return false;

    State& value = state();
    const std::uint64_t revision = Models::resourceRevision();
    if (value.resource_revision != revision) {
        value.cache.clear();
        value.resource_revision = revision;
    }

    const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
    if (!camera.valid || camera.projection != Camera::Projection::Perspective) return true;

    int width = 0;
    int height = 0;
    const GLuint color = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(output.color_texture));
    if (!textureSize(color, &width, &height)) return false;

    std::vector<ProjectedSplat> splats;
    for (const Systems::Scene::RenderItem& item : items) {
        if (!item.transform || !item.mesh_component || !item.mesh) continue;
        const Models::GaussianSplat::Data *data = nullptr;
        std::string error;
        if (!decoded(item, &data, &error)) {
            std::fprintf(stderr, "[GaussianSplat/OpenGL]: %s\n", error.c_str());
            return false;
        }
        const Math::Mat4 model = Math::modelMatrix(*item.transform);
        for (const Models::GaussianSplat::Splat& source : data->splats) {
            ProjectedSplat projected;
            if (GaussianSplat::project(
                    source,
                    data->spherical_harmonic_degree,
                    model,
                    camera,
                    width,
                    height,
                    &projected))
                splats.push_back(projected);
        }
    }
    if (splats.empty()) return true;

    std::stable_sort(splats.begin(), splats.end(), [](const ProjectedSplat& a, const ProjectedSplat& b) {
        return a.distance_squared > b.distance_squared;
    });

    GLint previous_framebuffer = 0;
    if (!attachFrame(output, &width, &height, &previous_framebuffer)) return false;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glViewport(0, 0, width, height);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (output.depth == Internal::DepthSource::Native) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    value.program.use();
    if (value.scene_depth_uniform >= 0) GL20.glUniform1i(value.scene_depth_uniform, 0);
    if (value.linear_depth_uniform >= 0)
        GL20.glUniform1i(value.linear_depth_uniform,
            output.depth == Internal::DepthSource::LinearTexture && output.depth_texture ? 1 : 0);
    if (value.inverse_size_uniform >= 0)
        GL20.glUniform2f(value.inverse_size_uniform,
            1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
    if (value.alpha_threshold_uniform >= 0)
        GL20.glUniform1f(value.alpha_threshold_uniform, AlphaThreshold);

    if (output.depth == Internal::DepthSource::LinearTexture && output.depth_texture) {
        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,
            static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(output.depth_texture)));
    }

    glBegin(GL_TRIANGLES);
    for (const ProjectedSplat& splat : splats) drawSplat(splat);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0u);
    Systems::OpenGL::unbindProgram();
    glPopClientAttrib();
    glPopAttrib();
    value.bind_framebuffer(GL_FRAMEBUFFER_EXT, static_cast<GLuint>(previous_framebuffer));
    return true;
}

void shutdown()
{
    State& value = state();
    value.cache.clear();
    value.resource_revision = 0u;
    if (value.framebuffer != 0u && value.delete_framebuffers)
        value.delete_framebuffers(1, &value.framebuffer);
    value.framebuffer = 0u;
    value.program.destroy();
    value.initialized = false;
}

} // namespace Renderer::GaussianSplat::OpenGL
