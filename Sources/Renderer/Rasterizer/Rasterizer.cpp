#include "Renderer/Rasterizer/Rasterizer.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Rasterizer/RasterizerShaders.hpp"
#include "Renderer/Scene.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
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

namespace Renderer {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kShadowResolution = 512;
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

Math::Mat4 cameraView(const Scene::CameraState& camera)
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
    std::fprintf(stderr, "[Rasterizer]: shader compile failed: %s\n", log.data());
    GL20.glDeleteShader(shader);
    return 0u;
}

GLuint createProgram(const char *vertex_source, const char *fragment_source)
{
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

    GLint status = 0;
    GL20.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint length = 0;
        GL20.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        GL20.glGetProgramInfoLog(program, length, nullptr, log.data());
        std::fprintf(stderr, "[Rasterizer]: program link failed: %s\n", log.data());
        GL20.glDeleteProgram(program);
        GL20.glDeleteShader(vertex);
        GL20.glDeleteShader(fragment);
        return 0u;
    }

    GL20.glDetachShader(program, vertex);
    GL20.glDetachShader(program, fragment);
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);
    return program;
}

GLint uniform(GLuint program, const char *name)
{
    return GL20.glGetUniformLocation(program, name);
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

    bool initialized = false;
    bool enabled = true;
    int width = 1;
    int height = 1;
    std::unordered_map<std::uint32_t, unsigned int> textures;
    std::vector<Scene::RenderItem> render_items;

    GLuint main_program = 0u;
    GLuint shadow_program = 0u;
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

    unsigned int textureFor(std::uint32_t handle)
    {
        if (handle == Models::INVALID_TEXTURE) return 0u;
        const auto found = textures.find(handle);
        if (found != textures.end()) return found->second;

        const Models::TextureAsset* asset = Models::texture(handle);
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
            return 0u;
        }

        GLModern.glActiveTexture(GL_TEXTURE0);
        GLuint texture_id = 0u;
        glGenTextures(1, &texture_id);
        if (texture_id == 0u) return 0u;

        glBindTexture(GL_TEXTURE_2D, texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            asset->image.width,
            asset->image.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            asset->image.rgba.data()
        );

        textures.emplace(handle, texture_id);
        return texture_id;
    }

    void clearTextures()
    {
        for (const auto& entry : textures) {
            const GLuint id = static_cast<GLuint>(entry.second);
            if (id != 0u) glDeleteTextures(1, &id);
        }
        textures.clear();
    }

    bool createPrograms()
    {
        main_program = createProgram(
            RasterizerShaders::main_vertex,
            RasterizerShaders::main_fragment
        );
        shadow_program = createProgram(
            RasterizerShaders::shadow_vertex,
            RasterizerShaders::shadow_fragment
        );
        if (main_program == 0u || shadow_program == 0u) return false;

        main_uniforms.diffuse = uniform(main_program, "uDiffuse");
        main_uniforms.gi[0] = uniform(main_program, "uGi0");
        main_uniforms.gi[1] = uniform(main_program, "uGi1");
        main_uniforms.gi[2] = uniform(main_program, "uGi2");
        main_uniforms.gi[3] = uniform(main_program, "uGi3");
        main_uniforms.shadow[0] = uniform(main_program, "uShadow0");
        main_uniforms.shadow[1] = uniform(main_program, "uShadow1");
        main_uniforms.shadow[2] = uniform(main_program, "uShadow2");
        main_uniforms.shadow[3] = uniform(main_program, "uShadow3");
        main_uniforms.shadow[4] = uniform(main_program, "uShadow4");
        main_uniforms.shadow[5] = uniform(main_program, "uShadow5");
        main_uniforms.has_texture = uniform(main_program, "uHasTexture");
        main_uniforms.has_gi = uniform(main_program, "uHasGi");
        main_uniforms.light_type = uniform(main_program, "uLightType");
        main_uniforms.has_shadow = uniform(main_program, "uHasShadow");
        main_uniforms.base_color = uniform(main_program, "uBaseColor");
        main_uniforms.light_position = uniform(main_program, "uLightPosition");
        main_uniforms.light_color = uniform(main_program, "uLightColor");
        main_uniforms.light_intensity = uniform(main_program, "uLightIntensity");
        main_uniforms.gi_minimum = uniform(main_program, "uGiMinimum");
        main_uniforms.gi_maximum = uniform(main_program, "uGiMaximum");
        main_uniforms.gi_intensity = uniform(main_program, "uGiIntensity");
        main_uniforms.shadow_far = uniform(main_program, "uShadowFar");
        main_uniforms.shadow_texel = uniform(main_program, "uShadowTexel");
        main_uniforms.model = uniform(main_program, "uModel");
        main_uniforms.normal_matrix = uniform(main_program, "uNormalMatrix");
        main_uniforms.shadow_matrix[0] = uniform(main_program, "uShadowMatrix0");
        main_uniforms.shadow_matrix[1] = uniform(main_program, "uShadowMatrix1");
        main_uniforms.shadow_matrix[2] = uniform(main_program, "uShadowMatrix2");
        main_uniforms.shadow_matrix[3] = uniform(main_program, "uShadowMatrix3");
        main_uniforms.shadow_matrix[4] = uniform(main_program, "uShadowMatrix4");
        main_uniforms.shadow_matrix[5] = uniform(main_program, "uShadowMatrix5");

        shadow_uniforms.diffuse = uniform(shadow_program, "uDiffuse");
        shadow_uniforms.has_texture = uniform(shadow_program, "uHasTexture");
        shadow_uniforms.base_alpha = uniform(shadow_program, "uBaseAlpha");
        shadow_uniforms.light_position = uniform(shadow_program, "uLightPosition");
        shadow_uniforms.shadow_far = uniform(shadow_program, "uShadowFar");
        shadow_uniforms.model = uniform(shadow_program, "uModel");

        GL20.glUseProgram(main_program);
        setInt(main_uniforms.diffuse, 0);
        for (int i = 0; i < 4; ++i) setInt(main_uniforms.gi[i], kGiTextureUnit + i);
        for (int i = 0; i < 6; ++i) setInt(main_uniforms.shadow[i], kShadowTextureUnit + i);
        GL20.glUseProgram(shadow_program);
        setInt(shadow_uniforms.diffuse, 0);
        GL20.glUseProgram(0u);
        return true;
    }

    void destroyPrograms()
    {
        if (main_program != 0u) GL20.glDeleteProgram(main_program);
        if (shadow_program != 0u) GL20.glDeleteProgram(shadow_program);
        main_program = 0u;
        shadow_program = 0u;
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

    std::uint64_t currentShadowSignature(const Scene::LightState& light) const
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
        for (const Scene::RenderItem& item : render_items) {
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
        for (const Scene::RenderItem& item : render_items) {
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
        return std::max(std::sqrt(far_distance_squared) * 1.05f, 1.0f);
    }

    void clearShadowTextures()
    {
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

    bool ensureShadowTextures(int requested_size)
    {
        const int size = std::max(requested_size, 1);
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
        const Vec3 up = Math::normalize(Math::cross(right[static_cast<std::size_t>(face)], forward[static_cast<std::size_t>(face)]));
        return Math::viewMatrix(
            position,
            forward[static_cast<std::size_t>(face)],
            right[static_cast<std::size_t>(face)],
            up
        );
    }

    void drawGeometry(bool shadow_pass)
    {
        for (const Scene::RenderItem& item : render_items) {
            const Models::MeshData* mesh = item.mesh;
            if (!mesh || mesh->indices.empty() || !item.transform) continue;

            const Models::MaterialData* material = item.material;
            const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
            if (opacity < 0.5f) continue;

            const unsigned int texture_id = material ? textureFor(material->diffuse_texture) : 0u;
            GLModern.glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_id));

            const Math::Mat4 model = Math::modelMatrix(*item.transform);
            if (shadow_pass) {
                setInt(shadow_uniforms.has_texture, texture_id != 0u ? 1 : 0);
                setFloat(shadow_uniforms.base_alpha, opacity);
                setMatrix(shadow_uniforms.model, model);
            } else {
                const Vec3 base_color = material
                    ? Vec3{material->color.x, material->color.y, material->color.z}
                    : Vec3{1.0f, 1.0f, 1.0f};
                setInt(main_uniforms.has_texture, texture_id != 0u ? 1 : 0);
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

    bool renderPointShadowMaps(const Scene::LightState& light)
    {
        if (!light.valid || light.light.type != LightType::Point || light.light.intensity <= 0.0f) {
            shadow_valid = false;
            return false;
        }

        const int requested_size = std::max(64, std::min({kShadowResolution, width, height}));
        if (!ensureShadowTextures(requested_size)) return false;

        const std::uint64_t signature = currentShadowSignature(light);
        if (shadow_valid && shadow_signature == signature) return true;

        shadow_far = calculateShadowFar(light.transform.position);
        const Math::Mat4 projection = perspectiveMatrix(90.0f, 1.0f, 0.05f, shadow_far);

        glDisable(GL_BLEND);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glViewport(0, 0, shadow_size, shadow_size);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

        GL20.glUseProgram(shadow_program);
        setVec3(shadow_uniforms.light_position, light.transform.position);
        setFloat(shadow_uniforms.shadow_far, shadow_far);

        for (int face = 0; face < 6; ++face) {
            const Math::Mat4 view = shadowView(light.transform.position, face);
            shadow_matrices[static_cast<std::size_t>(face)] = Math::multiply(projection, view);

            glMatrixMode(GL_PROJECTION);
            glLoadMatrixf(projection.data());
            glMatrixMode(GL_MODELVIEW);
            glLoadMatrixf(view.data());
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            drawGeometry(true);

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

        GL20.glUseProgram(0u);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glViewport(0, 0, width, height);
        glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
        shadow_signature = signature;
        shadow_valid = true;
        return true;
    }

    void bindGlobalState(
        const Scene::LightState& light,
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

    void draw(const Ecs::World& world, const GlobalIllumination::Field *gi)
    {
        Scene::collectRenderItems(world, render_items);
        const Scene::LightState light = Scene::lightState(world);
        renderPointShadowMaps(light);

        glViewport(0, 0, width, height);
        glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const Scene::CameraState camera = Scene::cameraState(world);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        applyInfinitePerspective(
            camera.valid ? camera.fov_degrees : 60.0f,
            static_cast<float>(width) / static_cast<float>(height),
            camera.valid ? camera.near_plane : 0.1f
        );

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        if (camera.valid) {
            const Math::Mat4 view = cameraView(camera);
            glMultMatrixf(view.data());
        }

        glDisable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        GL20.glUseProgram(main_program);
        bindGlobalState(light, gi);
        drawGeometry(false);
        GL20.glUseProgram(0u);

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

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_LIGHTING);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
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

    if (!impl_->enabled) {
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
    impl_->initialized = false;
}

bool Rasterizer::initialized() const
{
    return impl_ && impl_->initialized;
}

bool Rasterizer::enabled() const
{
    return impl_ && impl_->enabled;
}

void Rasterizer::setEnabled(bool enabled)
{
    if (impl_) impl_->enabled = enabled;
}

} // namespace Renderer
