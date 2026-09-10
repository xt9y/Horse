#include "Renderer/Rasterizer/Rasterizer.hpp"

#include "Camera.hpp"
#include "Models/Models.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Rasterizer/RasterizerShaders.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/OpenGL/TextureCache.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_set>
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
#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT 0x8D40
#endif
#ifndef GL_RENDERBUFFER_EXT
#define GL_RENDERBUFFER_EXT 0x8D41
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
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

namespace Renderer {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kGiTextureUnit = 1;
constexpr int kShadowTextureUnit = 5;

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float lengthSquared(Vec3 value)
{
    return Math::dot(value, value);
}

Math::Mat4 transpose(const Math::Mat4& matrix)
{
    Math::Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[static_cast<std::size_t>(column * 4 + row)] =
                matrix[static_cast<std::size_t>(row * 4 + column)];
        }
    }
    return result;
}

Math::Mat4 perspectiveMatrix(float fov_degrees, float aspect, float near_plane, float far_plane)
{
    const float safe_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = std::max(aspect, 1.0e-6f);
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float safe_far = std::max(far_plane, safe_near + 1.0e-3f);
    const float focal = 1.0f / std::tan(safe_fov * (kPi / 360.0f));
    const float range = safe_near - safe_far;

    return {
        focal / safe_aspect, 0.0f, 0.0f, 0.0f,
        0.0f, focal, 0.0f, 0.0f,
        0.0f, 0.0f, (safe_far + safe_near) / range, -1.0f,
        0.0f, 0.0f, (2.0f * safe_far * safe_near) / range, 0.0f,
    };
}

void applyInfinitePerspective(float fov_degrees, float aspect, float near_plane)
{
    const float safe_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float focal = 1.0f / std::tan(safe_fov * (kPi / 360.0f));

    const GLfloat projection[16] = {
        focal / safe_aspect, 0.0f, 0.0f, 0.0f,
        0.0f, focal, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, -1.0f,
        0.0f, 0.0f, -2.0f * safe_near, 0.0f,
    };
    glLoadMatrixf(projection);
}

Math::Mat4 cameraView(const Systems::Scene::CameraState& camera)
{
    const Vec3 forward = Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    const Vec3 right = Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    const Vec3 up = Math::normalize(Math::cross(right, forward));
    return Math::viewMatrix(camera.transform.position, forward, right, up);
}

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

void hashVec3(std::uint64_t& hash, Vec3 value)
{
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashTransform(std::uint64_t& hash, const Transform& transform)
{
    hashVec3(hash, transform.position);
    hashVec3(hash, transform.rotation);
    hashVec3(hash, transform.scale);
}

void setInt(GLint location, int value)
{
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(GLint location, float value)
{
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setVec3(GLint location, Vec3 value)
{
    if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z);
}

void setVec4(GLint location, float x, float y, float z, float w)
{
    if (location >= 0) GL20.glUniform4f(location, x, y, z, w);
}

void setMatrix(GLint location, const Math::Mat4& matrix)
{
    if (location >= 0) GL20.glUniformMatrix4fv(location, 1, GL_FALSE, matrix.data());
}

} // namespace

struct Rasterizer::Impl {
    using GenFramebuffersProc = void (*)(GLsizei, GLuint *);
    using DeleteFramebuffersProc = void (*)(GLsizei, const GLuint *);
    using BindFramebufferProc = void (*)(GLenum, GLuint);
    using FramebufferTexture2DProc = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferStatusProc = GLenum (*)(GLenum);
    using GenRenderbuffersProc = void (*)(GLsizei, GLuint *);
    using DeleteRenderbuffersProc = void (*)(GLsizei, const GLuint *);
    using BindRenderbufferProc = void (*)(GLenum, GLuint);
    using RenderbufferStorageProc = void (*)(GLenum, GLenum, GLsizei, GLsizei);
    using FramebufferRenderbufferProc = void (*)(GLenum, GLenum, GLenum, GLuint);

    struct MainUniforms {
        GLint diffuse = -1;
        GLint gi[4] {-1, -1, -1, -1};
        GLint shadow[6] {-1, -1, -1, -1, -1, -1};
        GLint has_texture = -1;
        GLint has_gi = -1;
        GLint light_type = -1;
        GLint has_shadow = -1;
        GLint base_color = -1;
        GLint light_position = -1;
        GLint light_color = -1;
        GLint light_intensity = -1;
        GLint gi_minimum = -1;
        GLint gi_maximum = -1;
        GLint gi_intensity = -1;
        GLint shadow_far = -1;
        GLint shadow_texel = -1;
        GLint model = -1;
        GLint normal_matrix = -1;
        GLint shadow_matrix[6] {-1, -1, -1, -1, -1, -1};
    };

    struct ShadowUniforms {
        GLint diffuse = -1;
        GLint has_texture = -1;
        GLint base_alpha = -1;
        GLint light_position = -1;
        GLint shadow_far = -1;
        GLint model = -1;
    };

    RasterizerSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    Systems::OpenGL::TextureCache textures;
    std::vector<Systems::Scene::RenderItem> render_items;
    std::unordered_set<Ecs::Entity> viewport_visible;
    bool viewport_filter_valid = false;

    Systems::OpenGL::Program main_program;
    Systems::OpenGL::Program shadow_program;
    MainUniforms main_uniforms{};
    ShadowUniforms shadow_uniforms{};

    std::array<GLuint, 4> gi_textures{};
    std::uint64_t gi_revision = 0u;
    bool gi_uploaded = false;

    std::array<GLuint, 6> shadow_textures{};
    std::array<Math::Mat4, 6> shadow_matrices{};
    std::uint64_t shadow_signature = 0u;
    float shadow_far = 1.0f;
    int shadow_size = 0;
    bool shadow_valid = false;

    GenFramebuffersProc glGenFramebuffers = nullptr;
    DeleteFramebuffersProc glDeleteFramebuffers = nullptr;
    BindFramebufferProc glBindFramebuffer = nullptr;
    FramebufferTexture2DProc glFramebufferTexture2D = nullptr;
    CheckFramebufferStatusProc glCheckFramebufferStatus = nullptr;
    GenRenderbuffersProc glGenRenderbuffers = nullptr;
    DeleteRenderbuffersProc glDeleteRenderbuffers = nullptr;
    BindRenderbufferProc glBindRenderbuffer = nullptr;
    RenderbufferStorageProc glRenderbufferStorage = nullptr;
    FramebufferRenderbufferProc glFramebufferRenderbuffer = nullptr;
    GLuint shadow_framebuffer = 0u;
    GLuint shadow_depth_renderbuffer = 0u;
    bool shadow_framebuffer_available = false;

    void applyClearColor() const
    {
        glClearColor(
            settings.clear_color.x,
            settings.clear_color.y,
            settings.clear_color.z,
            settings.clear_color.w
        );
    }

    static GLFWglproc resolveFramebufferProc(const char *core, const char *extension)
    {
        GLFWglproc result = glfwGetProcAddress(core);
        if (!result) result = glfwGetProcAddress(extension);
        return result;
    }

    bool loadShadowFramebufferApi()
    {
        glGenFramebuffers = reinterpret_cast<GenFramebuffersProc>(
            resolveFramebufferProc("glGenFramebuffers", "glGenFramebuffersEXT")
        );
        glDeleteFramebuffers = reinterpret_cast<DeleteFramebuffersProc>(
            resolveFramebufferProc("glDeleteFramebuffers", "glDeleteFramebuffersEXT")
        );
        glBindFramebuffer = reinterpret_cast<BindFramebufferProc>(
            resolveFramebufferProc("glBindFramebuffer", "glBindFramebufferEXT")
        );
        glFramebufferTexture2D = reinterpret_cast<FramebufferTexture2DProc>(
            resolveFramebufferProc("glFramebufferTexture2D", "glFramebufferTexture2DEXT")
        );
        glCheckFramebufferStatus = reinterpret_cast<CheckFramebufferStatusProc>(
            resolveFramebufferProc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT")
        );
        glGenRenderbuffers = reinterpret_cast<GenRenderbuffersProc>(
            resolveFramebufferProc("glGenRenderbuffers", "glGenRenderbuffersEXT")
        );
        glDeleteRenderbuffers = reinterpret_cast<DeleteRenderbuffersProc>(
            resolveFramebufferProc("glDeleteRenderbuffers", "glDeleteRenderbuffersEXT")
        );
        glBindRenderbuffer = reinterpret_cast<BindRenderbufferProc>(
            resolveFramebufferProc("glBindRenderbuffer", "glBindRenderbufferEXT")
        );
        glRenderbufferStorage = reinterpret_cast<RenderbufferStorageProc>(
            resolveFramebufferProc("glRenderbufferStorage", "glRenderbufferStorageEXT")
        );
        glFramebufferRenderbuffer = reinterpret_cast<FramebufferRenderbufferProc>(
            resolveFramebufferProc("glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT")
        );

        shadow_framebuffer_available =
            glGenFramebuffers && glDeleteFramebuffers && glBindFramebuffer &&
            glFramebufferTexture2D && glCheckFramebufferStatus &&
            glGenRenderbuffers && glDeleteRenderbuffers && glBindRenderbuffer &&
            glRenderbufferStorage && glFramebufferRenderbuffer;
        return shadow_framebuffer_available;
    }

    unsigned int fallbackTexture()
    {
        return textures.white();
    }

    unsigned int textureFor(std::uint32_t handle)
    {
        return textures.texture(handle);
    }

    void clearTextures()
    {
        textures.clear();
    }

    bool createPrograms()
    {
        if (!main_program.createGraphics(
                RasterizerShaders::main_vertex,
                RasterizerShaders::main_fragment,
                "Rasterizer"))
        {
            return false;
        }
        if (!shadow_program.createGraphics(
                RasterizerShaders::shadow_vertex,
                RasterizerShaders::shadow_fragment,
                "Rasterizer Shadow"))
        {
            main_program.destroy();
            return false;
        }

        main_uniforms.diffuse = main_program.uniform("uDiffuse");
        main_uniforms.gi[0] = main_program.uniform("uGi0");
        main_uniforms.gi[1] = main_program.uniform("uGi1");
        main_uniforms.gi[2] = main_program.uniform("uGi2");
        main_uniforms.gi[3] = main_program.uniform("uGi3");
        main_uniforms.shadow[0] = main_program.uniform("uShadow0");
        main_uniforms.shadow[1] = main_program.uniform("uShadow1");
        main_uniforms.shadow[2] = main_program.uniform("uShadow2");
        main_uniforms.shadow[3] = main_program.uniform("uShadow3");
        main_uniforms.shadow[4] = main_program.uniform("uShadow4");
        main_uniforms.shadow[5] = main_program.uniform("uShadow5");
        main_uniforms.has_texture = main_program.uniform("uHasTexture");
        main_uniforms.has_gi = main_program.uniform("uHasGi");
        main_uniforms.light_type = main_program.uniform("uLightType");
        main_uniforms.has_shadow = main_program.uniform("uHasShadow");
        main_uniforms.base_color = main_program.uniform("uBaseColor");
        main_uniforms.light_position = main_program.uniform("uLightPosition");
        main_uniforms.light_color = main_program.uniform("uLightColor");
        main_uniforms.light_intensity = main_program.uniform("uLightIntensity");
        main_uniforms.gi_minimum = main_program.uniform("uGiMinimum");
        main_uniforms.gi_maximum = main_program.uniform("uGiMaximum");
        main_uniforms.gi_intensity = main_program.uniform("uGiIntensity");
        main_uniforms.shadow_far = main_program.uniform("uShadowFar");
        main_uniforms.shadow_texel = main_program.uniform("uShadowTexel");
        main_uniforms.model = main_program.uniform("uModel");
        main_uniforms.normal_matrix = main_program.uniform("uNormalMatrix");
        main_uniforms.shadow_matrix[0] = main_program.uniform("uShadowMatrix0");
        main_uniforms.shadow_matrix[1] = main_program.uniform("uShadowMatrix1");
        main_uniforms.shadow_matrix[2] = main_program.uniform("uShadowMatrix2");
        main_uniforms.shadow_matrix[3] = main_program.uniform("uShadowMatrix3");
        main_uniforms.shadow_matrix[4] = main_program.uniform("uShadowMatrix4");
        main_uniforms.shadow_matrix[5] = main_program.uniform("uShadowMatrix5");

        shadow_uniforms.diffuse = shadow_program.uniform("uDiffuse");
        shadow_uniforms.has_texture = shadow_program.uniform("uHasTexture");
        shadow_uniforms.base_alpha = shadow_program.uniform("uBaseAlpha");
        shadow_uniforms.light_position = shadow_program.uniform("uLightPosition");
        shadow_uniforms.shadow_far = shadow_program.uniform("uShadowFar");
        shadow_uniforms.model = shadow_program.uniform("uModel");

        main_program.use();
        setInt(main_uniforms.diffuse, 0);
        for (int i = 0; i < 4; ++i) setInt(main_uniforms.gi[i], kGiTextureUnit + i);
        for (int i = 0; i < 6; ++i) setInt(main_uniforms.shadow[i], kShadowTextureUnit + i);
        shadow_program.use();
        setInt(shadow_uniforms.diffuse, 0);
        Systems::OpenGL::unbindProgram();
        return true;
    }

    void destroyPrograms()
    {
        main_program.destroy();
        shadow_program.destroy();
        main_uniforms = {};
        shadow_uniforms = {};
    }

    bool ensureGiTextures()
    {
        if (gi_textures[0] != 0u) return true;
        glGenTextures(static_cast<GLsizei>(gi_textures.size()), gi_textures.data());
        for (GLuint texture : gi_textures) {
            if (texture == 0u) return false;
        }
        return true;
    }

    bool uploadGlobalIllumination(const GlobalIllumination::Field *gi)
    {
        if (!gi || !gi->valid()) {
            gi_uploaded = false;
            return false;
        }
        if (gi_uploaded && gi_revision == gi->revision) return true;
        if (!ensureGiTextures()) return false;

        const std::size_t probe_count = gi->probes.size();
        std::vector<float> data(probe_count * 4u, 0.0f);
        for (std::size_t coefficient = 0u; coefficient < 4u; ++coefficient) {
            for (std::size_t probe = 0u; probe < probe_count; ++probe) {
                const Vec3 value = gi->probes[probe].sh[coefficient];
                const std::size_t offset = probe * 4u;
                data[offset + 0u] = value.x;
                data[offset + 1u] = value.y;
                data[offset + 2u] = value.z;
                data[offset + 3u] = 0.0f;
            }

            GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kGiTextureUnit + coefficient));
            glBindTexture(GL_TEXTURE_3D, gi_textures[coefficient]);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            GLModern.glTexImage3D(
                GL_TEXTURE_3D,
                0,
                GL_RGBA16F_ARB,
                static_cast<GLsizei>(gi->size_x),
                static_cast<GLsizei>(gi->size_y),
                static_cast<GLsizei>(gi->size_z),
                0,
                GL_RGBA,
                GL_FLOAT,
                data.data()
            );
        }
        GLModern.glActiveTexture(GL_TEXTURE0);
        gi_revision = gi->revision;
        gi_uploaded = true;
        return true;
    }

    void clearGiTextures()
    {
        for (GLuint texture : gi_textures) {
            if (texture != 0u) glDeleteTextures(1, &texture);
        }
        gi_textures.fill(0u);
        gi_revision = 0u;
        gi_uploaded = false;
    }

    std::uint64_t currentShadowSignature(const Systems::Scene::LightState& light) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        hashValue(hash, light.valid ? 1u : 0u);
        if (light.valid) {
            hashValue(hash, static_cast<std::uint32_t>(light.light.type));
            hashVec3(hash, light.light.color);
            hashFloat(hash, light.light.intensity);
            hashTransform(hash, light.transform);
        }
        hashValue(hash, static_cast<std::uint32_t>(render_items.size()));
        for (const Systems::Scene::RenderItem& item : render_items) {
            hashValue(hash, static_cast<std::uint32_t>(item.entity));
            if (item.mesh_component) {
                hashValue(hash, item.mesh_component->mesh);
                hashValue(hash, item.mesh_component->material);
            }
            if (item.transform) hashTransform(hash, *item.transform);
        }
        return hash;
    }

    float calculateShadowFar(Vec3 light_position) const
    {
        float far_distance_squared = 1.0f;
        for (const Systems::Scene::RenderItem& item : render_items) {
            if (!item.mesh || !item.transform) continue;
            const Math::Mat4 model = Math::modelMatrix(*item.transform);
            const Models::Vec3 model_minimum = item.mesh->bounds.minimum;
            const Models::Vec3 model_maximum = item.mesh->bounds.maximum;
            const Vec3 minimum {model_minimum.x, model_minimum.y, model_minimum.z};
            const Vec3 maximum {model_maximum.x, model_maximum.y, model_maximum.z};
            for (int x = 0; x < 2; ++x) {
                for (int y = 0; y < 2; ++y) {
                    for (int z = 0; z < 2; ++z) {
                        const Vec3 local {
                            x == 0 ? minimum.x : maximum.x,
                            y == 0 ? minimum.y : maximum.y,
                            z == 0 ? minimum.z : maximum.z,
                        };
                        const Vec3 world = Math::transformPoint(model, local);
                        far_distance_squared = std::max(
                            far_distance_squared,
                            lengthSquared(subtract(world, light_position))
                        );
                    }
                }
            }
        }
        const float scale = std::max(settings.shadow_far_scale, 0.0f);
        return std::max(std::sqrt(far_distance_squared) * scale, 1.0f);
    }

    void clearShadowFramebuffer()
    {
        if (shadow_depth_renderbuffer != 0u && glDeleteRenderbuffers) {
            glDeleteRenderbuffers(1, &shadow_depth_renderbuffer);
        }
        if (shadow_framebuffer != 0u && glDeleteFramebuffers) {
            glDeleteFramebuffers(1, &shadow_framebuffer);
        }
        shadow_depth_renderbuffer = 0u;
        shadow_framebuffer = 0u;
    }

    void clearShadowTextures()
    {
        clearShadowFramebuffer();
        for (GLuint texture : shadow_textures) {
            if (texture != 0u) glDeleteTextures(1, &texture);
        }
        shadow_textures.fill(0u);
        shadow_matrices.fill(Math::identityMatrix());
        shadow_signature = 0u;
        shadow_far = 1.0f;
        shadow_size = 0;
        shadow_valid = false;
    }

    bool createShadowFramebuffer()
    {
        if (!shadow_framebuffer_available) return false;

        glGenFramebuffers(1, &shadow_framebuffer);
        glGenRenderbuffers(1, &shadow_depth_renderbuffer);
        if (shadow_framebuffer == 0u || shadow_depth_renderbuffer == 0u) {
            clearShadowFramebuffer();
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER_EXT, shadow_framebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER_EXT, shadow_depth_renderbuffer);
        glRenderbufferStorage(
            GL_RENDERBUFFER_EXT,
            GL_DEPTH_COMPONENT24,
            shadow_size,
            shadow_size
        );
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER_EXT,
            GL_DEPTH_ATTACHMENT_EXT,
            GL_RENDERBUFFER_EXT,
            shadow_depth_renderbuffer
        );
        glFramebufferTexture2D(
            GL_FRAMEBUFFER_EXT,
            GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_2D,
            shadow_textures[0],
            0
        );
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
        glBindRenderbuffer(GL_RENDERBUFFER_EXT, 0u);
        glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0u);
        glDrawBuffer(GL_BACK);

        if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
            std::fprintf(
                stderr,
                "[Rasterizer]: shadow framebuffer incomplete (0x%x); using window-sized fallback\n",
                static_cast<unsigned int>(status)
            );
            clearShadowFramebuffer();
            shadow_framebuffer_available = false;
            return false;
        }
        return true;
    }

    bool ensureShadowTextures(int requested_size)
    {
        GLint max_texture_size = requested_size;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        const int size = std::max(1, std::min(requested_size, static_cast<int>(max_texture_size)));
        if (shadow_textures[0] != 0u && shadow_size == size) return true;

        clearShadowTextures();
        shadow_size = size;
        glGenTextures(static_cast<GLsizei>(shadow_textures.size()), shadow_textures.data());
        for (GLuint texture : shadow_textures) {
            if (texture == 0u) return false;
            GLModern.glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                shadow_size,
                shadow_size,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                nullptr
            );
        }

        if (shadow_framebuffer_available && !createShadowFramebuffer()) return false;
        shadow_valid = false;
        return true;
    }

    static Math::Mat4 shadowView(Vec3 position, int face)
    {
        static constexpr std::array<Vec3, 6> forward {{
            { 1.0f,  0.0f,  0.0f},
            {-1.0f,  0.0f,  0.0f},
            { 0.0f,  1.0f,  0.0f},
            { 0.0f, -1.0f,  0.0f},
            { 0.0f,  0.0f,  1.0f},
            { 0.0f,  0.0f, -1.0f},
        }};
        static constexpr std::array<Vec3, 6> right {{
            { 0.0f,  0.0f, -1.0f},
            { 0.0f,  0.0f,  1.0f},
            { 1.0f,  0.0f,  0.0f},
            { 1.0f,  0.0f,  0.0f},
            {-1.0f,  0.0f,  0.0f},
            { 1.0f,  0.0f,  0.0f},
        }};
        const Vec3 up = Math::normalize(Math::cross(
            right[static_cast<std::size_t>(face)],
            forward[static_cast<std::size_t>(face)]
        ));
        return Math::viewMatrix(
            position,
            forward[static_cast<std::size_t>(face)],
            right[static_cast<std::size_t>(face)],
            up
        );
    }

    bool itemVisible(const Systems::Scene::RenderItem& item, bool shadow_pass) const
    {
        if (shadow_pass || !settings.viewport_culling || !viewport_filter_valid) return true;
        return viewport_visible.find(item.entity) != viewport_visible.end();
    }

    void drawGeometry(bool shadow_pass)
    {
        for (const Systems::Scene::RenderItem& item : render_items) {
            if (!itemVisible(item, shadow_pass)) continue;

            const Models::MeshData* mesh = item.mesh;
            if (!mesh || mesh->indices.empty() || !item.transform) continue;

            const Models::MaterialData* material = item.material;
            const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
            if (opacity < 0.5f) continue;

            const bool requested_texture = material && material->diffuse_texture != Models::INVALID_TEXTURE;
            const unsigned int texture_id = requested_texture ? textureFor(material->diffuse_texture) : 0u;
            const bool has_texture = requested_texture && texture_id != 0u;
            const unsigned int bound_texture = texture_id != 0u ? texture_id : fallbackTexture();
            if (bound_texture == 0u) continue;

            GLModern.glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound_texture));

            const Math::Mat4 model = Math::modelMatrix(*item.transform);
            if (shadow_pass) {
                setInt(shadow_uniforms.has_texture, has_texture ? 1 : 0);
                setFloat(shadow_uniforms.base_alpha, opacity);
                setMatrix(shadow_uniforms.model, model);
            } else {
                const Vec3 base_color = material
                    ? Vec3{material->color.x, material->color.y, material->color.z}
                    : Vec3{1.0f, 1.0f, 1.0f};
                setInt(main_uniforms.has_texture, has_texture ? 1 : 0);
                setVec4(main_uniforms.base_color, base_color.x, base_color.y, base_color.z, opacity);
                setMatrix(main_uniforms.model, model);
                setMatrix(main_uniforms.normal_matrix, transpose(Math::inverseModelMatrix(*item.transform)));

                if (opacity < 0.999f) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glDisable(GL_BLEND);
                }
            }

            glPushMatrix();
            glMultMatrixf(model.data());
            glBegin(GL_TRIANGLES);
            for (const std::uint32_t index : mesh->indices) {
                if (index >= mesh->vertices.size()) continue;
                const Models::Vertex& vertex = mesh->vertices[index];
                glNormal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z);
                glTexCoord2f(vertex.uv.x, 1.0f - vertex.uv.y);
                glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            glEnd();
            glPopMatrix();
        }
    }

    bool renderPointShadowMaps(const Systems::Scene::LightState& light)
    {
        if (!light.valid || light.light.type != LightType::Point || light.light.intensity <= 0.0f) {
            shadow_valid = false;
            return false;
        }

        const int requested_size = std::max(settings.shadow_resolution, 1);
        const int minimum_size = std::max(settings.minimum_shadow_resolution, 1);
        const int fallback_size = std::max(
            minimum_size,
            std::min({std::max(settings.fallback_shadow_resolution, 1), width, height})
        );
        const int target_size = shadow_framebuffer_available ? requested_size : fallback_size;
        if (!ensureShadowTextures(target_size)) {
            if (shadow_framebuffer_available) {
                shadow_framebuffer_available = false;
                if (!ensureShadowTextures(fallback_size)) return false;
            } else {
                return false;
            }
        }

        const std::uint64_t signature = currentShadowSignature(light);
        if (shadow_valid && shadow_signature == signature) return true;

        shadow_far = calculateShadowFar(light.transform.position);
        const Math::Mat4 projection = perspectiveMatrix(
            90.0f,
            1.0f,
            std::max(settings.shadow_near_plane, 1.0e-4f),
            shadow_far
        );

        for (int i = 0; i < 6; ++i) {
            GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kShadowTextureUnit + i));
            glBindTexture(GL_TEXTURE_2D, 0u);
        }
        GLModern.glActiveTexture(GL_TEXTURE0);

        const bool offscreen =
            shadow_framebuffer_available && shadow_framebuffer != 0u &&
            shadow_depth_renderbuffer != 0u;

        if (offscreen) {
            glBindFramebuffer(GL_FRAMEBUFFER_EXT, shadow_framebuffer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        }

        glDisable(GL_BLEND);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glViewport(0, 0, shadow_size, shadow_size);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        shadow_program.use();
        setVec3(shadow_uniforms.light_position, light.transform.position);
        setFloat(shadow_uniforms.shadow_far, shadow_far);

        for (int face = 0; face < 6; ++face) {
            const Math::Mat4 view = shadowView(light.transform.position, face);
            shadow_matrices[static_cast<std::size_t>(face)] = Math::multiply(projection, view);

            if (offscreen) {
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER_EXT,
                    GL_COLOR_ATTACHMENT0_EXT,
                    GL_TEXTURE_2D,
                    shadow_textures[static_cast<std::size_t>(face)],
                    0
                );
            }

            glMatrixMode(GL_PROJECTION);
            glLoadMatrixf(projection.data());
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(view.data());
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            drawGeometry(true);

            if (!offscreen) {
                GLModern.glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, shadow_textures[static_cast<std::size_t>(face)]);
                glCopyTexSubImage2D(
                    GL_TEXTURE_2D,
                    0,
                    0,
                    0,
                    0,
                    0,
                    shadow_size,
                    shadow_size
                );
            }
        }

        Systems::OpenGL::unbindProgram();
        if (offscreen) {
            glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0u);
            glDrawBuffer(GL_BACK);
        }

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glViewport(0, 0, width, height);
        applyClearColor();
        shadow_signature = signature;
        shadow_valid = true;
        return true;
    }

    void bindGlobalState(
        const Systems::Scene::LightState& light,
        const GlobalIllumination::Field *gi)
    {
        int light_type = 0;
        if (light.valid) {
            if (light.light.type == LightType::Point) light_type = 1;
            else if (light.light.type == LightType::Directional) light_type = 2;
        }
        setInt(main_uniforms.light_type, light_type);
        setVec3(main_uniforms.light_position, light.valid ? light.transform.position : Vec3{});
        setVec3(main_uniforms.light_color, light.valid ? light.light.color : Vec3{});
        setFloat(main_uniforms.light_intensity, light.valid ? std::max(light.light.intensity, 0.0f) : 0.0f);

        const bool has_gi = uploadGlobalIllumination(gi);
        setInt(main_uniforms.has_gi, has_gi ? 1 : 0);
        if (has_gi && gi) {
            setVec3(main_uniforms.gi_minimum, gi->minimum);
            setVec3(main_uniforms.gi_maximum, gi->maximum);
            setFloat(main_uniforms.gi_intensity, std::max(gi->intensity, 0.0f));
            for (int i = 0; i < 4; ++i) {
                GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kGiTextureUnit + i));
                glBindTexture(GL_TEXTURE_3D, gi_textures[static_cast<std::size_t>(i)]);
            }
        } else {
            setFloat(main_uniforms.gi_intensity, 0.0f);
        }

        const bool has_shadow = shadow_valid && light_type == 1;
        setInt(main_uniforms.has_shadow, has_shadow ? 1 : 0);
        setFloat(main_uniforms.shadow_far, has_shadow ? shadow_far : 1.0f);
        setFloat(main_uniforms.shadow_texel, has_shadow ? 1.0f / static_cast<float>(shadow_size) : 0.0f);
        if (has_shadow) {
            for (int i = 0; i < 6; ++i) {
                GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kShadowTextureUnit + i));
                glBindTexture(GL_TEXTURE_2D, shadow_textures[static_cast<std::size_t>(i)]);
                setMatrix(main_uniforms.shadow_matrix[i], shadow_matrices[static_cast<std::size_t>(i)]);
            }
        }
        GLModern.glActiveTexture(GL_TEXTURE0);
    }

    void updateViewportVisibility(const Ecs::World& world)
    {
        viewport_visible.clear();
        viewport_filter_valid = false;
        if (!settings.viewport_culling) return;

        const Visibility::Result visibility = Visibility::system().evaluate(world, width, height);
        if (!visibility.frustum.valid) return;

        viewport_visible.insert(visibility.visible.begin(), visibility.visible.end());
        viewport_filter_valid = true;
    }

    void draw(const Ecs::World& world, const GlobalIllumination::Field *gi)
    {
        Systems::Scene::collectRenderItems(world, render_items);
        updateViewportVisibility(world);

        const Systems::Scene::LightState light = Systems::Scene::lightState(world);
        renderPointShadowMaps(light);

        glViewport(0, 0, width, height);
        applyClearColor();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const Systems::Scene::CameraState camera = Systems::Scene::cameraState(world);
        if (!camera.valid) return;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        applyInfinitePerspective(
            camera.fov_degrees,
            static_cast<float>(width) / static_cast<float>(height),
            camera.near_plane
        );

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const Math::Mat4 view = cameraView(camera);
        glMultMatrixf(view.data());

        glDisable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        main_program.use();
        bindGlobalState(light, gi);
        drawGeometry(false);
        Systems::OpenGL::unbindProgram();

        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0u);
        glDisable(GL_BLEND);
    }
};

Rasterizer::Rasterizer()
    : impl_(new Impl())
{
}

Rasterizer::~Rasterizer()
{
    shutdown();
    delete impl_;
}

bool Rasterizer::init()
{
    if (impl_->initialized) return true;

    if (glGetString(GL_VERSION) == nullptr) {
        std::fprintf(stderr, "[Rasterizer]: OpenGL initialization failed: no current context\n");
        return false;
    }
    if (lwcglLoadModernGL() != 0 || !GL20.glCreateShader || !GLModern.glTexImage3D) {
        std::fprintf(
            stderr,
            "[Rasterizer]: OpenGL 2.1 shader/3D-texture support unavailable: %s\n",
            lwcglModernGLMissingFunction() ? lwcglModernGLMissingFunction() : "unknown"
        );
        return false;
    }
    if (!impl_->createPrograms()) {
        impl_->destroyPrograms();
        return false;
    }
    if (impl_->fallbackTexture() == 0u) {
        std::fprintf(stderr, "[Rasterizer]: failed to create complete diffuse fallback texture\n");
        impl_->destroyPrograms();
        return false;
    }

    if (!impl_->loadShadowFramebufferApi()) {
        std::fprintf(
            stderr,
            "[Rasterizer]: offscreen shadow framebuffer unavailable; using window-sized shadow fallback\n"
        );
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_LIGHTING);
    glShadeModel(GL_SMOOTH);
    impl_->applyClearColor();
    glViewport(0, 0, impl_->width, impl_->height);
    impl_->initialized = true;

    std::fprintf(stderr, "[Rasterizer]: OpenGL shader backend active\n");
    return true;
}

void Rasterizer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    if (impl_->initialized) glViewport(0, 0, impl_->width, impl_->height);
}

bool Rasterizer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_->initialized) return false;

    if (!impl_->settings.enabled) {
        impl_->applyClearColor();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
        impl_->draw(world, output.global_illumination);
    }

    output.api = Internal::GraphicsApi::OpenGL;
    output.depth = Internal::DepthSource::Native;
    output.width = impl_->width;
    output.height = impl_->height;
    return true;
}

void Rasterizer::present(Internal::FrameOutput& output)
{
    (void)output;
}

void Rasterizer::shutdown()
{
    if (!impl_ || !impl_->initialized) return;
    Internal::shutdownFonts(Internal::GraphicsApi::OpenGL);
    impl_->clearTextures();
    impl_->clearGiTextures();
    impl_->clearShadowTextures();
    impl_->destroyPrograms();
    impl_->render_items.clear();
    impl_->viewport_visible.clear();
    impl_->viewport_filter_valid = false;
    impl_->initialized = false;
}

bool Rasterizer::initialized() const
{
    return impl_ && impl_->initialized;
}

bool Rasterizer::enabled() const
{
    return impl_ && impl_->settings.enabled;
}

void Rasterizer::setEnabled(bool enabled)
{
    if (impl_) impl_->settings.enabled = enabled;
}

void Rasterizer::setViewportCulling(bool value)
{
    if (impl_) impl_->settings.viewport_culling = value;
}

void Rasterizer::setShadowResolution(int value)
{
    if (!impl_) return;
    impl_->settings.shadow_resolution = value;
    impl_->shadow_valid = false;
}

void Rasterizer::setFallbackShadowResolution(int value)
{
    if (!impl_) return;
    impl_->settings.fallback_shadow_resolution = value;
    impl_->shadow_valid = false;
}

void Rasterizer::setMinimumShadowResolution(int value)
{
    if (!impl_) return;
    impl_->settings.minimum_shadow_resolution = value;
    impl_->shadow_valid = false;
}

void Rasterizer::setShadowNearPlane(float value)
{
    if (!impl_) return;
    impl_->settings.shadow_near_plane = value;
    impl_->shadow_valid = false;
}

void Rasterizer::setShadowFarScale(float value)
{
    if (!impl_) return;
    impl_->settings.shadow_far_scale = value;
    impl_->shadow_valid = false;
}

void Rasterizer::setClearColor(Vec4 value)
{
    if (impl_) impl_->settings.clear_color = value;
}

bool Rasterizer::viewportCulling() const
{
    return impl_ && impl_->settings.viewport_culling;
}

int Rasterizer::shadowResolution() const
{
    return impl_ ? impl_->settings.shadow_resolution : 0;
}

int Rasterizer::fallbackShadowResolution() const
{
    return impl_ ? impl_->settings.fallback_shadow_resolution : 0;
}

int Rasterizer::minimumShadowResolution() const
{
    return impl_ ? impl_->settings.minimum_shadow_resolution : 0;
}

float Rasterizer::shadowNearPlane() const
{
    return impl_ ? impl_->settings.shadow_near_plane : 0.0f;
}

float Rasterizer::shadowFarScale() const
{
    return impl_ ? impl_->settings.shadow_far_scale : 0.0f;
}

Vec4 Rasterizer::clearColor() const
{
    return impl_ ? impl_->settings.clear_color : Vec4{};
}

RasterizerSettings& Rasterizer::settings()
{
    return impl_->settings;
}

const RasterizerSettings& Rasterizer::settings() const
{
    return impl_->settings;
}

} // namespace Renderer
