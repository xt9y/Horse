#include "Renderer/GaussianSplat/OpenGL/GaussianSplatOpenGL.hpp"

#include "Models/GaussianSplat.hpp"
#include "Models/Models.hpp"
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
#include <cmath>
#include <cstddef>
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

constexpr float Pi = 3.14159265358979323846f;
constexpr float SphericalHarmonic0 = 0.28209479177387814f;
constexpr float MinimumVariancePixels = 0.25f;
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
        if (vLinearDepth > scene_depth + 0.0025) discard;
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

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct DrawSplat {
    Vec2 center{};
    Vec2 axis0{};
    Vec2 axis1{};
    Vec3 color{};
    float opacity = 1.0f;
    float ndc_depth = 0.0f;
    float linear_depth = 0.0f;
    float distance_squared = 0.0f;
};

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

    value.gen_framebuffers = reinterpret_cast<GenFramebuffersProc>(
        resolve("glGenFramebuffers", "glGenFramebuffersEXT"));
    value.delete_framebuffers = reinterpret_cast<DeleteFramebuffersProc>(
        resolve("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    value.bind_framebuffer = reinterpret_cast<BindFramebufferProc>(
        resolve("glBindFramebuffer", "glBindFramebufferEXT"));
    value.framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DProc>(
        resolve("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    value.check_framebuffer = reinterpret_cast<CheckFramebufferStatusProc>(
        resolve("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
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

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float lengthSquared(Vec3 value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vec3 localAxis(const Models::GaussianSplat::Splat& splat, int axis)
{
    const float x = splat.rotation.x;
    const float y = splat.rotation.y;
    const float z = splat.rotation.z;
    const float w = splat.rotation.w;
    if (axis == 0) {
        return {
            splat.scale.x * (1.0f - 2.0f * (y * y + z * z)),
            splat.scale.x * (2.0f * (x * y + w * z)),
            splat.scale.x * (2.0f * (x * z - w * y)),
        };
    }
    if (axis == 1) {
        return {
            splat.scale.y * (2.0f * (x * y - w * z)),
            splat.scale.y * (1.0f - 2.0f * (x * x + z * z)),
            splat.scale.y * (2.0f * (y * z + w * x)),
        };
    }
    return {
        splat.scale.z * (2.0f * (x * z + w * y)),
        splat.scale.z * (2.0f * (y * z - w * x)),
        splat.scale.z * (1.0f - 2.0f * (x * x + y * y)),
    };
}

Vec3 diffuseColor(const Models::GaussianSplat::Splat& splat)
{
    if (splat.spherical_harmonics.empty()) return {0.5f, 0.5f, 0.5f};
    const Models::Vec3 coefficient = splat.spherical_harmonics.front();
    return {
        std::max(coefficient.x * SphericalHarmonic0 + 0.5f, 0.0f),
        std::max(coefficient.y * SphericalHarmonic0 + 0.5f, 0.0f),
        std::max(coefficient.z * SphericalHarmonic0 + 0.5f, 0.0f),
    };
}

bool projectedAxis(
    Vec3 world_axis,
    Vec3 relative,
    const Systems::CameraState& camera,
    float depth,
    float tangent,
    float aspect,
    Vec2 *out)
{
    if (!out || depth <= 1.0e-6f || tangent <= 1.0e-8f || aspect <= 1.0e-8f) return false;
    const float x = Math::dot(relative, camera.right);
    const float y = Math::dot(relative, camera.up);
    const float dx = Math::dot(world_axis, camera.right);
    const float dy = Math::dot(world_axis, camera.up);
    const float dz = Math::dot(world_axis, camera.forward);
    const float inverse_depth2 = 1.0f / (depth * depth);
    out->x = (dx * depth - x * dz) * inverse_depth2 / (aspect * tangent);
    out->y = (dy * depth - y * dz) * inverse_depth2 / tangent;
    return std::isfinite(out->x) && std::isfinite(out->y);
}

float ndcDepth(float depth, float near_plane, float far_plane)
{
    if (far_plane > near_plane) {
        return (far_plane + near_plane) / (far_plane - near_plane) -
            (2.0f * far_plane * near_plane) / ((far_plane - near_plane) * depth);
    }
    return 1.0f - 2.0f * near_plane / depth;
}

bool project(
    const Models::GaussianSplat::Splat& splat,
    const Math::Mat4& model,
    const Systems::CameraState& camera,
    int width,
    int height,
    DrawSplat *out)
{
    if (!out || width <= 0 || height <= 0 || camera.projection != Camera::Projection::Perspective)
        return false;

    const Vec3 world = Math::transformPoint(model, {
        splat.position.x,
        splat.position.y,
        splat.position.z,
    });
    const Vec3 relative = subtract(world, camera.position);
    const float depth = Math::dot(relative, camera.forward);
    const float near_plane = std::max(camera.near_plane, 1.0e-4f);
    if (depth <= near_plane || (camera.far_plane > near_plane && depth >= camera.far_plane)) return false;

    const float tangent = std::tan(std::clamp(camera.fov_degrees, 1.0f, 179.0f) * Pi / 360.0f);
    const float aspect = camera.aspect_ratio > 0.0f
        ? camera.aspect_ratio
        : static_cast<float>(width) / static_cast<float>(height);
    const float camera_x = Math::dot(relative, camera.right);
    const float camera_y = Math::dot(relative, camera.up);
    const float center_x = camera_x / (depth * aspect * tangent);
    const float center_y = camera_y / (depth * tangent);

    Vec2 projected[3];
    for (int axis = 0; axis < 3; ++axis) {
        const Vec3 local = localAxis(splat, axis);
        const Vec3 world_axis = Math::transformVector(model, local);
        if (!projectedAxis(world_axis, relative, camera, depth, tangent, aspect, &projected[axis]))
            return false;
    }

    float covariance_xx = 0.0f;
    float covariance_xy = 0.0f;
    float covariance_yy = 0.0f;
    for (const Vec2& axis : projected) {
        covariance_xx += axis.x * axis.x;
        covariance_xy += axis.x * axis.y;
        covariance_yy += axis.y * axis.y;
    }
    const float pixel_x = 2.0f / static_cast<float>(width);
    const float pixel_y = 2.0f / static_cast<float>(height);
    covariance_xx += MinimumVariancePixels * pixel_x * pixel_x;
    covariance_yy += MinimumVariancePixels * pixel_y * pixel_y;

    const float trace = covariance_xx + covariance_yy;
    const float determinant = covariance_xx * covariance_yy - covariance_xy * covariance_xy;
    const float discriminant = std::sqrt(std::max(trace * trace * 0.25f - determinant, 0.0f));
    const float lambda0 = std::max(trace * 0.5f + discriminant, 0.0f);
    const float lambda1 = std::max(trace * 0.5f - discriminant, 0.0f);
    if (lambda0 <= 1.0e-16f) return false;

    Vec2 eigen0;
    if (std::abs(covariance_xy) > 1.0e-12f) {
        eigen0 = {lambda0 - covariance_yy, covariance_xy};
    } else {
        eigen0 = covariance_xx >= covariance_yy ? Vec2{1.0f, 0.0f} : Vec2{0.0f, 1.0f};
    }
    const float eigen_length = std::sqrt(eigen0.x * eigen0.x + eigen0.y * eigen0.y);
    if (eigen_length <= 1.0e-12f) return false;
    eigen0.x /= eigen_length;
    eigen0.y /= eigen_length;
    const Vec2 eigen1 {-eigen0.y, eigen0.x};

    const float extent0 = 3.0f * std::sqrt(lambda0);
    const float extent1 = 3.0f * std::sqrt(lambda1);
    out->center = {center_x, center_y};
    out->axis0 = {eigen0.x * extent0, eigen0.y * extent0};
    out->axis1 = {eigen1.x * extent1, eigen1.y * extent1};
    out->color = diffuseColor(splat);
    out->opacity = splat.opacity;
    out->ndc_depth = std::clamp(ndcDepth(depth, near_plane, camera.far_plane), -1.0f, 1.0f);
    out->linear_depth = depth;
    out->distance_squared = lengthSquared(relative);

    const float radius_x = std::abs(out->axis0.x) + std::abs(out->axis1.x);
    const float radius_y = std::abs(out->axis0.y) + std::abs(out->axis1.y);
    return center_x + radius_x >= -1.0f && center_x - radius_x <= 1.0f &&
        center_y + radius_y >= -1.0f && center_y - radius_y <= 1.0f;
}

bool decoded(
    const Systems::Scene::RenderItem& item,
    const Models::GaussianSplat::Data **out,
    std::string *error)
{
    if (!out || !item.mesh || !item.mesh_component) return false;
    State& value = state();
    const Models::MeshHandle handle = item.mesh_component->mesh;
    auto found = value.cache.find(handle);
    if (found == value.cache.end()) {
        CacheEntry entry;
        if (!Models::GaussianSplat::decode(*item.mesh, &entry.data, error)) return false;
        found = value.cache.emplace(handle, std::move(entry)).first;
    }
    *out = &found->second.data;
    return true;
}

void emitVertex(const DrawSplat& splat, float x, float y)
{
    const float px = splat.center.x + splat.axis0.x * x / 3.0f + splat.axis1.x * y / 3.0f;
    const float py = splat.center.y + splat.axis0.y * x / 3.0f + splat.axis1.y * y / 3.0f;
    glNormal3f(splat.linear_depth, 0.0f, 0.0f);
    glTexCoord2f(x, y);
    glColor4f(splat.color.x, splat.color.y, splat.color.z, splat.opacity);
    glVertex4f(px, py, splat.ndc_depth, 1.0f);
}

void drawSplat(const DrawSplat& splat)
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
    if (color == 0u || !textureSize(color, width, height)) return false;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, previous_framebuffer);
    value.bind_framebuffer(GL_FRAMEBUFFER_EXT, value.framebuffer);
    value.framebuffer_texture_2d(
        GL_FRAMEBUFFER_EXT,
        GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_2D,
        color,
        0
    );
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
    if (items.empty()) return true;
    if (!output.color_texture) return true;
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

    std::vector<DrawSplat> splats;
    for (const Systems::Scene::RenderItem& item : items) {
        if (!item.transform || !item.mesh_component || !item.mesh) continue;
        const Models::GaussianSplat::Data *data = nullptr;
        std::string error;
        if (!decoded(item, &data, &error)) {
            std::fprintf(stderr, "[GaussianSplat]: %s\n", error.c_str());
            return false;
        }
        const Math::Mat4 model = Math::modelMatrix(*item.transform);
        for (const Models::GaussianSplat::Splat& source : data->splats) {
            DrawSplat projected;
            if (project(source, model, camera, width, height, &projected))
                splats.push_back(projected);
        }
    }
    if (splats.empty()) return true;

    std::stable_sort(
        splats.begin(),
        splats.end(),
        [](const DrawSplat& a, const DrawSplat& b) {
            return a.distance_squared > b.distance_squared;
        }
    );

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
        GL20.glUniform1i(
            value.linear_depth_uniform,
            output.depth == Internal::DepthSource::LinearTexture && output.depth_texture ? 1 : 0
        );
    if (value.inverse_size_uniform >= 0)
        GL20.glUniform2f(
            value.inverse_size_uniform,
            1.0f / static_cast<float>(width),
            1.0f / static_cast<float>(height)
        );
    if (value.alpha_threshold_uniform >= 0) GL20.glUniform1f(value.alpha_threshold_uniform, AlphaThreshold);

    if (output.depth == Internal::DepthSource::LinearTexture && output.depth_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(
            GL_TEXTURE_2D,
            static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(output.depth_texture))
        );
    }

    glBegin(GL_TRIANGLES);
    for (const DrawSplat& splat : splats) drawSplat(splat);
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
