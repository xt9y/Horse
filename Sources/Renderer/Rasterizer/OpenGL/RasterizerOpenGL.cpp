#include "Renderer/Rasterizer/Rasterizer.hpp"

#include "Camera.hpp"
#include "Models/Models.hpp"
#include "Renderer/Environment.hpp"
#include "Renderer/Frame/OpenGL/FrameOpenGL.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Rasterizer/OpenGL/GlobalIlluminationTextures.hpp"
#include "Renderer/Rasterizer/OpenGL/ShadowTarget.hpp"
#include "Renderer/Rasterizer/RasterizerShaders.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/OpenGL/TextureCache.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"
#include "Renderer/Trace/CameraProjection.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#endif

namespace Renderer {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kGiTextureUnit = 1;
constexpr int kShadowTextureUnit = 5;
constexpr int kNormalTextureUnit = 11;
constexpr int kRoughnessTextureUnit = 12;
constexpr int kMetallicTextureUnit = 13;
constexpr int kAoTextureUnit = 14;
constexpr int kEmissiveTextureUnit = 15;
constexpr int kEnvironmentTextureUnit = 16;

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float lengthSquared(Vec3 value) { return Math::dot(value, value); }

Math::Mat4 transpose(const Math::Mat4& matrix)
{
    Math::Mat4 result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[static_cast<std::size_t>(column * 4 + row)] =
                matrix[static_cast<std::size_t>(row * 4 + column)];
    return result;
}

Math::Mat4 infinitePerspectiveMatrix(float fov_degrees, float aspect, float near_plane)
{
    const float safe_fov = std::clamp(fov_degrees, 1.0f, 179.0f);
    const float safe_aspect = aspect > 1.0e-6f ? aspect : 1.0f;
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float focal = 1.0f / std::tan(safe_fov * (kPi / 360.0f));
    return {
        focal / safe_aspect, 0.0f, 0.0f, 0.0f,
        0.0f, focal, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, -1.0f,
        0.0f, 0.0f, -2.0f * safe_near, 0.0f,
    };
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

Math::Mat4 orthographicMatrix(
    float half_width,
    float half_height,
    float near_plane,
    float far_plane)
{
    const float safe_width = std::max(std::abs(half_width), 1.0e-3f);
    const float safe_height = std::max(std::abs(half_height), 1.0e-3f);
    const float safe_near = std::max(near_plane, 1.0e-4f);
    const float safe_far = std::max(far_plane, safe_near + 1.0e-3f);
    const float range = safe_far - safe_near;
    return {
        1.0f / safe_width, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f / safe_height, 0.0f, 0.0f,
        0.0f, 0.0f, -2.0f / range, 0.0f,
        0.0f, 0.0f, -(safe_far + safe_near) / range, 1.0f,
    };
}

Math::Mat4 orthographicMatrix(float half_extent, float near_plane, float far_plane)
{
    return orthographicMatrix(half_extent, half_extent, near_plane, far_plane);
}

Math::Mat4 cameraProjectionMatrix(const Systems::CameraState& camera, float viewport_aspect)
{
    if (camera.projection == Camera::Projection::Orthographic)
        return orthographicMatrix(camera.xmag, camera.ymag, camera.near_plane, camera.far_plane);

    const float aspect = camera.aspect_ratio > 1.0e-6f
        ? camera.aspect_ratio
        : std::max(viewport_aspect, 1.0e-6f);
    if (camera.far_plane > camera.near_plane + 1.0e-4f)
        return perspectiveMatrix(camera.fov_degrees, aspect, camera.near_plane, camera.far_plane);
    return infinitePerspectiveMatrix(camera.fov_degrees, aspect, camera.near_plane);
}

Math::Mat4 cameraView(const Systems::CameraState& camera)
{
    return Math::viewMatrix(camera.position, camera.forward, camera.right, camera.up);
}

GLenum primitiveMode(Models::PrimitiveMode mode)
{
    switch (mode) {
        case Models::PrimitiveMode::Points: return GL_POINTS;
        case Models::PrimitiveMode::Lines: return GL_LINES;
        case Models::PrimitiveMode::LineLoop: return GL_LINE_LOOP;
        case Models::PrimitiveMode::LineStrip: return GL_LINE_STRIP;
        case Models::PrimitiveMode::TriangleStrip: return GL_TRIANGLE_STRIP;
        case Models::PrimitiveMode::TriangleFan: return GL_TRIANGLE_FAN;
        case Models::PrimitiveMode::Triangles:
        default: return GL_TRIANGLES;
    }
}

Vec3 lightDirection(const Systems::Scene::LightState& light)
{
    if (!light.valid) return {0.0f, -1.0f, 0.0f};
    return Math::normalize(Math::transformNormal(Math::modelMatrix(light.transform), {0.0f, 0.0f, -1.0f}));
}

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}
void hashFloat(std::uint64_t& hash, float value) { hashValue(hash, std::bit_cast<std::uint32_t>(value)); }
void hashVec3(std::uint64_t& hash, Vec3 value) { hashFloat(hash, value.x); hashFloat(hash, value.y); hashFloat(hash, value.z); }
void hashTransform(std::uint64_t& hash, const Transform& transform)
{
    hashValue(hash, transform.matrix_override_enabled ? 1u : 0u);
    if (transform.matrix_override_enabled) {
        for (float value : transform.matrix_override) hashFloat(hash, value);
        return;
    }
    hashVec3(hash, transform.position); hashVec3(hash, transform.rotation); hashVec3(hash, transform.scale);
}
void setInt(GLint location, int value) { if (location >= 0) GL20.glUniform1i(location, value); }
void setFloat(GLint location, float value) { if (location >= 0) GL20.glUniform1f(location, value); }
void setVec3(GLint location, Vec3 value) { if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z); }
void setVec4(GLint location, float x, float y, float z, float w) { if (location >= 0) GL20.glUniform4f(location, x, y, z, w); }
void setMatrix(GLint location, const Math::Mat4& matrix) { if (location >= 0) GL20.glUniformMatrix4fv(location, 1, GL_FALSE, matrix.data()); }

} // namespace

struct Rasterizer::Impl {
    struct MainUniforms {
        GLint diffuse = -1, normal_map = -1, roughness_map = -1, metallic_map = -1, ao_map = -1, emissive_map = -1, environment = -1;
        GLint gi[4] {-1, -1, -1, -1};
        GLint shadow[6] {-1, -1, -1, -1, -1, -1};
        GLint has_texture = -1, has_normal_map = -1, has_roughness_map = -1, has_metallic_map = -1, has_ao_map = -1, has_emissive_map = -1;
        GLint has_environment_texture = -1, has_gi = -1, light_type = -1, has_shadow = -1, fog_mode = -1;
        GLint base_color = -1, roughness = -1, metallic = -1, ao = -1, emissive_color = -1, emissive_strength = -1;
        GLint camera_position = -1, light_position = -1, light_direction = -1, light_color = -1, light_intensity = -1, light_range = -1;
        GLint spot_inner_cos = -1, spot_outer_cos = -1, environment_average = -1, environment_intensity = -1, environment_rotation = -1;
        GLint ambient_color = -1, ambient_intensity = -1, fog_color = -1, fog_density = -1, fog_start = -1, fog_end = -1;
        GLint gi_minimum = -1, gi_maximum = -1, gi_intensity = -1, shadow_far = -1, shadow_texel = -1, alpha_cutoff = -1;
        GLint model = -1, previous_model = -1, current_view_projection = -1, previous_view_projection = -1, normal_matrix = -1;
        GLint shadow_matrix[6] {-1, -1, -1, -1, -1, -1};
    };

    struct SkyUniforms {
        GLint environment = -1, has_environment_texture = -1, sky_color = -1, environment_intensity = -1, environment_rotation = -1;
        GLint camera_forward = -1, camera_right = -1, camera_up = -1;
        GLint previous_camera_forward = -1, previous_camera_right = -1, previous_camera_up = -1;
        GLint tan_half_fov = -1, aspect = -1;
    };

    struct ShadowUniforms {
        GLint diffuse = -1, has_texture = -1, base_alpha = -1, light_position = -1, shadow_far = -1, alpha_cutoff = -1, directional = -1, model = -1;
    };

    RasterizerSettings settings{};
    bool initialized = false;
    int width = 1, height = 1;
    bool environment_specular_texture_supported = false;
    Systems::OpenGL::TextureCache textures;
    Frame::OpenGL::Target frame_target;
    std::vector<Systems::Scene::RenderItem> render_items;
    std::unordered_set<Ecs::Entity> viewport_visible;
    bool viewport_filter_valid = false;
    std::vector<Math::Mat4> previous_models;
    std::vector<std::uint8_t> previous_model_valid;
    Math::Mat4 current_view_projection = Math::identityMatrix();
    Math::Mat4 previous_view_projection = Math::identityMatrix();
    Systems::CameraState previous_camera{};
    bool history_valid = false;

    Systems::OpenGL::Program main_program, sky_program, shadow_program;
    MainUniforms main_uniforms{};
    SkyUniforms sky_uniforms{};
    ShadowUniforms shadow_uniforms{};

    RasterizerOpenGL::GlobalIlluminationTextures gi_textures;
    RasterizerOpenGL::ShadowTarget shadow_target;
    std::array<Math::Mat4, 6> shadow_matrices{};
    std::uint64_t shadow_signature = 0u;
    float shadow_far = 1.0f;
    bool shadow_valid = false;

    void applyClearColor() const { glClearColor(settings.clear_color.x, settings.clear_color.y, settings.clear_color.z, settings.clear_color.w); }

    unsigned int fallbackTexture() { return textures.white(); }
    unsigned int textureFor(Models::TextureHandle handle) { return handle == Models::INVALID_TEXTURE ? 0u : textures.texture(handle); }
    void clearTextures() { textures.clear(); }

    bool createPrograms()
    {
        if (!main_program.createGraphics(RasterizerShaders::main_vertex, RasterizerShaders::main_fragment, "Rasterizer")) return false;
        if (!sky_program.createGraphics(RasterizerShaders::sky_vertex, RasterizerShaders::sky_fragment, "Rasterizer Sky")) { main_program.destroy(); return false; }
        if (!shadow_program.createGraphics(RasterizerShaders::shadow_vertex, RasterizerShaders::shadow_fragment, "Rasterizer Shadow")) {
            sky_program.destroy(); main_program.destroy(); return false;
        }
#define U(target, name) target = main_program.uniform(name)
        U(main_uniforms.diffuse, "uDiffuse"); U(main_uniforms.normal_map, "uNormalMap"); U(main_uniforms.roughness_map, "uRoughnessMap");
        U(main_uniforms.metallic_map, "uMetallicMap"); U(main_uniforms.ao_map, "uAoMap"); U(main_uniforms.emissive_map, "uEmissiveMap"); U(main_uniforms.environment, "uEnvironment");
        U(main_uniforms.has_texture, "uHasTexture"); U(main_uniforms.has_normal_map, "uHasNormalMap"); U(main_uniforms.has_roughness_map, "uHasRoughnessMap");
        U(main_uniforms.has_metallic_map, "uHasMetallicMap"); U(main_uniforms.has_ao_map, "uHasAoMap"); U(main_uniforms.has_emissive_map, "uHasEmissiveMap");
        U(main_uniforms.has_environment_texture, "uHasEnvironmentTexture"); U(main_uniforms.has_gi, "uHasGi"); U(main_uniforms.light_type, "uLightType");
        U(main_uniforms.has_shadow, "uHasShadow"); U(main_uniforms.fog_mode, "uFogMode"); U(main_uniforms.base_color, "uBaseColor");
        U(main_uniforms.roughness, "uRoughness"); U(main_uniforms.metallic, "uMetallic"); U(main_uniforms.ao, "uAo");
        U(main_uniforms.emissive_color, "uEmissiveColor"); U(main_uniforms.emissive_strength, "uEmissiveStrength"); U(main_uniforms.camera_position, "uCameraPosition");
        U(main_uniforms.light_position, "uLightPosition"); U(main_uniforms.light_direction, "uLightDirection"); U(main_uniforms.light_color, "uLightColor");
        U(main_uniforms.light_intensity, "uLightIntensity"); U(main_uniforms.light_range, "uLightRange"); U(main_uniforms.spot_inner_cos, "uSpotInnerCos"); U(main_uniforms.spot_outer_cos, "uSpotOuterCos");
        U(main_uniforms.environment_average, "uEnvironmentAverage"); U(main_uniforms.environment_intensity, "uEnvironmentIntensity"); U(main_uniforms.environment_rotation, "uEnvironmentRotation");
        U(main_uniforms.ambient_color, "uAmbientColor"); U(main_uniforms.ambient_intensity, "uAmbientIntensity"); U(main_uniforms.fog_color, "uFogColor");
        U(main_uniforms.fog_density, "uFogDensity"); U(main_uniforms.fog_start, "uFogStart"); U(main_uniforms.fog_end, "uFogEnd");
        U(main_uniforms.gi_minimum, "uGiMinimum"); U(main_uniforms.gi_maximum, "uGiMaximum"); U(main_uniforms.gi_intensity, "uGiIntensity");
        U(main_uniforms.shadow_far, "uShadowFar"); U(main_uniforms.shadow_texel, "uShadowTexel"); U(main_uniforms.alpha_cutoff, "uAlphaCutoff");
        U(main_uniforms.model, "uModel"); U(main_uniforms.previous_model, "uPreviousModel"); U(main_uniforms.current_view_projection, "uCurrentViewProjection");
        U(main_uniforms.previous_view_projection, "uPreviousViewProjection"); U(main_uniforms.normal_matrix, "uNormalMatrix");
#undef U
        for (int i = 0; i < 4; ++i) { char name[16]{}; std::snprintf(name, sizeof(name), "uGi%d", i); main_uniforms.gi[i] = main_program.uniform(name); }
        for (int i = 0; i < 6; ++i) {
            char name[24]{}; std::snprintf(name, sizeof(name), "uShadow%d", i); main_uniforms.shadow[i] = main_program.uniform(name);
            char matrix_name[32]{}; std::snprintf(matrix_name, sizeof(matrix_name), "uShadowMatrix%d", i); main_uniforms.shadow_matrix[i] = main_program.uniform(matrix_name);
        }
#define S(target, name) target = sky_program.uniform(name)
        S(sky_uniforms.environment, "uEnvironment"); S(sky_uniforms.has_environment_texture, "uHasEnvironmentTexture"); S(sky_uniforms.sky_color, "uSkyColor");
        S(sky_uniforms.environment_intensity, "uEnvironmentIntensity"); S(sky_uniforms.environment_rotation, "uEnvironmentRotation"); S(sky_uniforms.camera_forward, "uCameraForward");
        S(sky_uniforms.camera_right, "uCameraRight"); S(sky_uniforms.camera_up, "uCameraUp"); S(sky_uniforms.previous_camera_forward, "uPreviousCameraForward");
        S(sky_uniforms.previous_camera_right, "uPreviousCameraRight"); S(sky_uniforms.previous_camera_up, "uPreviousCameraUp"); S(sky_uniforms.tan_half_fov, "uTanHalfFov"); S(sky_uniforms.aspect, "uAspect");
#undef S
        shadow_uniforms.diffuse = shadow_program.uniform("uDiffuse"); shadow_uniforms.has_texture = shadow_program.uniform("uHasTexture");
        shadow_uniforms.base_alpha = shadow_program.uniform("uBaseAlpha"); shadow_uniforms.light_position = shadow_program.uniform("uLightPosition");
        shadow_uniforms.shadow_far = shadow_program.uniform("uShadowFar"); shadow_uniforms.alpha_cutoff = shadow_program.uniform("uAlphaCutoff");
        shadow_uniforms.directional = shadow_program.uniform("uDirectional"); shadow_uniforms.model = shadow_program.uniform("uModel");

        main_program.use();
        setInt(main_uniforms.diffuse, 0); setInt(main_uniforms.normal_map, kNormalTextureUnit); setInt(main_uniforms.roughness_map, kRoughnessTextureUnit);
        setInt(main_uniforms.metallic_map, kMetallicTextureUnit); setInt(main_uniforms.ao_map, kAoTextureUnit); setInt(main_uniforms.emissive_map, kEmissiveTextureUnit);
        if (environment_specular_texture_supported) setInt(main_uniforms.environment, kEnvironmentTextureUnit);
        for (int i = 0; i < 4; ++i) setInt(main_uniforms.gi[i], kGiTextureUnit + i);
        for (int i = 0; i < 6; ++i) setInt(main_uniforms.shadow[i], kShadowTextureUnit + i);
        sky_program.use(); setInt(sky_uniforms.environment, 0);
        shadow_program.use(); setInt(shadow_uniforms.diffuse, 0);
        Systems::OpenGL::unbindProgram();
        return true;
    }

    void destroyPrograms()
    {
        main_program.destroy(); sky_program.destroy(); shadow_program.destroy();
        main_uniforms = {}; sky_uniforms = {}; shadow_uniforms = {};
    }

    std::uint64_t currentShadowSignature(const Systems::Scene::LightState& light) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        hashValue(hash, light.valid ? 1u : 0u);
        if (light.valid) {
            hashValue(hash, static_cast<std::uint32_t>(light.light.type)); hashVec3(hash, light.light.color); hashFloat(hash, light.light.intensity);
            hashFloat(hash, light.light.range); hashFloat(hash, light.light.inner_cone_degrees); hashFloat(hash, light.light.outer_cone_degrees); hashTransform(hash, light.transform);
        }
        hashValue(hash, static_cast<std::uint32_t>(render_items.size()));
        for (const Systems::Scene::RenderItem& item : render_items) {
            hashValue(hash, static_cast<std::uint32_t>(item.entity));
            if (item.mesh_component) { hashValue(hash, item.mesh_component->mesh); hashValue(hash, item.mesh_component->material); }
            if (item.transform) hashTransform(hash, *item.transform);
        }
        return hash;
    }

    float calculateShadowFar(Vec3 light_position, float configured_range) const
    {
        if (configured_range > 0.0f) return configured_range;
        float far_squared = 1.0f;
        for (const Systems::Scene::RenderItem& item : render_items) {
            if (!item.mesh || !item.transform) continue;
            const Math::Mat4 model = Math::modelMatrix(*item.transform);
            const Vec3 minimum {item.mesh->bounds.minimum.x, item.mesh->bounds.minimum.y, item.mesh->bounds.minimum.z};
            const Vec3 maximum {item.mesh->bounds.maximum.x, item.mesh->bounds.maximum.y, item.mesh->bounds.maximum.z};
            for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < 2; ++z) {
                const Vec3 local {x == 0 ? minimum.x : maximum.x, y == 0 ? minimum.y : maximum.y, z == 0 ? minimum.z : maximum.z};
                const Vec3 world = Math::transformPoint(model, local);
                far_squared = std::max(far_squared, lengthSquared(subtract(world, light_position)));
            }
        }
        return std::max(std::sqrt(far_squared) * std::max(settings.shadow_far_scale, 0.0f), 1.0f);
    }

    void clearShadowTextures()
    {
        shadow_target.clear(); shadow_matrices.fill(Math::identityMatrix()); shadow_signature = 0u; shadow_far = 1.0f; shadow_valid = false;
    }

    static Math::Mat4 shadowView(Vec3 position, int face)
    {
        static constexpr std::array<Vec3, 6> forward {{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
        static constexpr std::array<Vec3, 6> right {{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{-1,0,0},{1,0,0}}};
        const Vec3 up = Math::normalize(Math::cross(right[static_cast<std::size_t>(face)], forward[static_cast<std::size_t>(face)]));
        return Math::viewMatrix(position, forward[static_cast<std::size_t>(face)], right[static_cast<std::size_t>(face)], up);
    }

    bool itemVisible(const Systems::Scene::RenderItem& item, bool shadow_pass) const
    {
        return shadow_pass || !settings.viewport_culling || !viewport_filter_valid || viewport_visible.find(item.entity) != viewport_visible.end();
    }

    Models::TextureHandle baseTexture(const Models::MaterialData *material, bool *opacity_only)
    {
        if (opacity_only) *opacity_only = false;
        if (!material) return Models::INVALID_TEXTURE;
        if (material->opacity_texture == Models::INVALID_TEXTURE) return material->diffuse_texture;
        if (material->diffuse_texture == Models::INVALID_TEXTURE) { if (opacity_only) *opacity_only = true; return material->opacity_texture; }
        std::string error;
        const Models::TextureHandle combined = Models::combineTextureOpacity(material->diffuse_texture, material->opacity_texture, &error);
        return combined != Models::INVALID_TEXTURE ? combined : material->diffuse_texture;
    }

    void bindMaterialMap(int unit, Models::TextureHandle handle, GLint has_uniform)
    {
        const GLuint texture = static_cast<GLuint>(textureFor(handle));
        setInt(has_uniform, texture != 0u ? 1 : 0);
        GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit)); glBindTexture(GL_TEXTURE_2D, texture);
    }

    void bindMaterial(const Models::MaterialData *material)
    {
        setFloat(main_uniforms.roughness, material ? std::clamp(material->roughness, 0.04f, 1.0f) : 0.8f);
        setFloat(main_uniforms.metallic, material ? std::clamp(material->metallic, 0.0f, 1.0f) : 0.0f);
        setFloat(main_uniforms.ao, material ? std::clamp(material->ambient_occlusion, 0.0f, 1.0f) : 1.0f);
        setVec3(main_uniforms.emissive_color, material ? Vec3{material->emissive_color.x, material->emissive_color.y, material->emissive_color.z} : Vec3{});
        setFloat(main_uniforms.emissive_strength, material ? std::max(material->emissive_strength, 0.0f) : 0.0f);
        bindMaterialMap(kNormalTextureUnit, material ? material->normal_texture : Models::INVALID_TEXTURE, main_uniforms.has_normal_map);
        bindMaterialMap(kRoughnessTextureUnit, material ? material->roughness_texture : Models::INVALID_TEXTURE, main_uniforms.has_roughness_map);
        bindMaterialMap(kMetallicTextureUnit, material ? material->metallic_texture : Models::INVALID_TEXTURE, main_uniforms.has_metallic_map);
        bindMaterialMap(kAoTextureUnit, material ? material->ambient_occlusion_texture : Models::INVALID_TEXTURE, main_uniforms.has_ao_map);
        bindMaterialMap(kEmissiveTextureUnit, material ? material->emissive_texture : Models::INVALID_TEXTURE, main_uniforms.has_emissive_map);
        GLModern.glActiveTexture(GL_TEXTURE0);
    }

    Math::Mat4 previousModel(Ecs::Entity entity, const Math::Mat4& current)
    {
        const std::size_t index = static_cast<std::size_t>(entity);
        if (previous_models.size() <= index) {
            previous_models.resize(index + 1u, Math::identityMatrix());
            previous_model_valid.resize(index + 1u, 0u);
        }
        return history_valid && previous_model_valid[index] != 0u ? previous_models[index] : current;
    }

    void storeModel(Ecs::Entity entity, const Math::Mat4& model)
    {
        const std::size_t index = static_cast<std::size_t>(entity);
        if (previous_models.size() <= index) {
            previous_models.resize(index + 1u, Math::identityMatrix());
            previous_model_valid.resize(index + 1u, 0u);
        }
        previous_models[index] = model; previous_model_valid[index] = 1u;
    }

    void drawGeometry(bool shadow_pass)
    {
        const float alpha_cutoff = std::clamp(Systems::SceneCache::opacityCutoff(), 0.0f, 1.0f);
        if (shadow_pass) setFloat(shadow_uniforms.alpha_cutoff, alpha_cutoff); else setFloat(main_uniforms.alpha_cutoff, alpha_cutoff);
        if (!shadow_pass) {
            setMatrix(main_uniforms.current_view_projection, current_view_projection);
            setMatrix(main_uniforms.previous_view_projection, history_valid ? previous_view_projection : current_view_projection);
        }
        for (const Systems::Scene::RenderItem& item : render_items) {
            const Models::MeshData* mesh = item.mesh;
            if (!mesh || !item.transform) continue;
            const std::vector<std::uint32_t>& draw_indices =
                mesh->source_indices.empty() ? mesh->indices : mesh->source_indices;
            if (draw_indices.empty()) continue;
            const Math::Mat4 model = Math::modelMatrix(*item.transform);
            if (!shadow_pass && !itemVisible(item, false)) { storeModel(item.entity, model); continue; }
            const Models::MaterialData* material = item.material;
            const float opacity = material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f;
            if (opacity < alpha_cutoff) { if (!shadow_pass) storeModel(item.entity, model); continue; }
            bool opacity_only = false;
            const GLuint texture_id = static_cast<GLuint>(textureFor(baseTexture(material, &opacity_only)));
            const bool has_texture = texture_id != 0u;
            const GLuint bound = has_texture ? texture_id : static_cast<GLuint>(fallbackTexture());
            if (bound == 0u) continue;
            GLModern.glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, bound);
            if (shadow_pass) {
                setInt(shadow_uniforms.has_texture, has_texture ? 1 : 0); setFloat(shadow_uniforms.base_alpha, opacity); setMatrix(shadow_uniforms.model, model);
            } else {
                const Vec3 base = material ? Vec3{material->color.x, material->color.y, material->color.z} : Vec3{1,1,1};
                setInt(main_uniforms.has_texture, has_texture ? 1 : 0); setVec4(main_uniforms.base_color, base.x, base.y, base.z, opacity);
                setMatrix(main_uniforms.model, model); setMatrix(main_uniforms.previous_model, previousModel(item.entity, model));
                setMatrix(main_uniforms.normal_matrix, transpose(Math::inverseModelMatrix(*item.transform))); bindMaterial(material);
                if (opacity < 0.999f) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); } else glDisable(GL_BLEND);
            }
            if (opacity_only) glColor4f(1,1,1,1);
            glPushMatrix(); glMultMatrixf(model.data()); glBegin(primitiveMode(mesh->primitive_mode));
            for (const std::uint32_t index : draw_indices) {
                if (index >= mesh->vertices.size()) continue;
                const Models::Vertex& vertex = mesh->vertices[index];
                glNormal3f(vertex.normal.x, vertex.normal.y, vertex.normal.z); glTexCoord2f(vertex.uv.x, 1.0f - vertex.uv.y);
                glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            glEnd(); glPopMatrix();
            if (!shadow_pass) storeModel(item.entity, model);
        }
    }

    bool renderLocalShadowMaps(const Systems::Scene::LightState& light)
    {
        if (!light.valid || (light.light.type != LightType::Point && light.light.type != LightType::Spot) || light.light.intensity <= 0.0f) { shadow_valid = false; return false; }
        const int requested = std::max(settings.shadow_resolution, 1);
        const int minimum = std::max(settings.minimum_shadow_resolution, 1);
        const int fallback = std::max(minimum, std::min({std::max(settings.fallback_shadow_resolution, 1), width, height}));
        const int target = shadow_target.framebufferAvailable() ? requested : fallback;
        const int previous_size = shadow_target.size();
        if (!shadow_target.ensure(target)) {
            if (shadow_target.framebufferAvailable()) { shadow_target.disableFramebuffer(); if (!shadow_target.ensure(fallback)) return false; }
            else return false;
        }
        if (shadow_target.size() != previous_size) shadow_valid = false;
        const std::uint64_t signature = currentShadowSignature(light);
        if (shadow_valid && shadow_signature == signature) return true;
        shadow_far = calculateShadowFar(light.transform.position, std::max(light.light.range, 0.0f));
        const Math::Mat4 projection = perspectiveMatrix(90.0f, 1.0f, std::max(settings.shadow_near_plane, 1.0e-4f), shadow_far);
        shadow_target.unbind(kShadowTextureUnit);
        const bool offscreen = shadow_target.offscreen();
        shadow_target.begin();
        glDisable(GL_BLEND); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
        glViewport(0,0,shadow_target.size(),shadow_target.size()); glClearColor(1,1,1,1);
        shadow_program.use(); setInt(shadow_uniforms.directional, 0); setVec3(shadow_uniforms.light_position, light.transform.position); setFloat(shadow_uniforms.shadow_far, shadow_far);
        for (int face = 0; face < 6; ++face) {
            const Math::Mat4 view = shadowView(light.transform.position, face);
            shadow_matrices[static_cast<std::size_t>(face)] = Math::multiply(projection, view);
            if (offscreen) shadow_target.selectFace(static_cast<std::size_t>(face));
            glMatrixMode(GL_PROJECTION); glLoadMatrixf(projection.data()); glMatrixMode(GL_MODELVIEW); glLoadMatrixf(view.data());
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); drawGeometry(true);
            if (!offscreen) shadow_target.captureFace(static_cast<std::size_t>(face));
        }
        Systems::OpenGL::unbindProgram();
        shadow_target.finish();
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glViewport(0,0,width,height); applyClearColor();
        shadow_signature = signature; shadow_valid = true; return true;
    }

    bool renderDirectionalShadowMap(
        const Systems::Scene::LightState& light,
        const Systems::CameraState& camera)
    {
        if (!light.valid || light.light.type != LightType::Directional ||
            light.light.intensity <= 0.0f || !camera.valid)
        {
            shadow_valid = false;
            return false;
        }

        const int requested = std::max(settings.shadow_resolution, 1);
        const int minimum = std::max(settings.minimum_shadow_resolution, 1);
        const int fallback = std::max(
            minimum,
            std::min({std::max(settings.fallback_shadow_resolution, 1), width, height})
        );
        const int target = shadow_target.framebufferAvailable() ? requested : fallback;
        const int previous_size = shadow_target.size();
        if (!shadow_target.ensure(target)) {
            if (shadow_target.framebufferAvailable()) {
                shadow_target.disableFramebuffer();
                if (!shadow_target.ensure(fallback)) return false;
            } else {
                return false;
            }
        }
        if (shadow_target.size() != previous_size) shadow_valid = false;

        const float extent = std::max(settings.directional_shadow_distance, 1.0f);
        std::uint64_t signature = currentShadowSignature(light);
        const std::uint64_t camera_signature = Systems::cameraSignature(camera);
        hashValue(signature, static_cast<std::uint32_t>(camera_signature));
        hashValue(signature, static_cast<std::uint32_t>(camera_signature >> 32u));
        hashFloat(signature, extent);
        if (shadow_valid && shadow_signature == signature) return true;

        const Vec3 direction = lightDirection(light);
        const Vec3 center {
            camera.position.x + camera.forward.x * extent * 0.25f,
            camera.position.y + camera.forward.y * extent * 0.25f,
            camera.position.z + camera.forward.z * extent * 0.25f,
        };
        const Vec3 light_position {
            center.x - direction.x * extent * 2.0f,
            center.y - direction.y * extent * 2.0f,
            center.z - direction.z * extent * 2.0f,
        };
        const Vec3 up_reference = std::abs(direction.y) > 0.95f
            ? Vec3{0.0f, 0.0f, 1.0f}
            : Vec3{0.0f, 1.0f, 0.0f};
        const Vec3 right = Math::normalize(Math::cross(direction, up_reference));
        const Vec3 up = Math::normalize(Math::cross(right, direction));
        shadow_far = extent * 4.0f;
        const Math::Mat4 projection = orthographicMatrix(
            extent,
            std::max(settings.shadow_near_plane, 1.0e-4f),
            shadow_far
        );
        const Math::Mat4 view = Math::viewMatrix(light_position, direction, right, up);
        shadow_matrices.fill(Math::identityMatrix());
        shadow_matrices[0] = Math::multiply(projection, view);

        shadow_target.unbind(kShadowTextureUnit);
        const bool offscreen = shadow_target.offscreen();
        shadow_target.begin();
        glDisable(GL_BLEND); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
        glViewport(0,0,shadow_target.size(),shadow_target.size()); glClearColor(1,1,1,1);
        shadow_program.use(); setInt(shadow_uniforms.directional, 1); setVec3(shadow_uniforms.light_position, light_position); setFloat(shadow_uniforms.shadow_far, shadow_far);
        if (offscreen) shadow_target.selectFace(0u);
        glMatrixMode(GL_PROJECTION); glLoadMatrixf(projection.data()); glMatrixMode(GL_MODELVIEW); glLoadMatrixf(view.data());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); drawGeometry(true);
        if (!offscreen) shadow_target.captureFace(0u);
        Systems::OpenGL::unbindProgram();
        shadow_target.finish();
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glViewport(0,0,width,height); applyClearColor();
        shadow_signature = signature; shadow_valid = true; return true;
    }

    bool renderShadowMaps(
        const Systems::Scene::LightState& light,
        const Systems::CameraState& camera)
    {
        if (light.valid && light.light.type == LightType::Directional)
            return renderDirectionalShadowMap(light, camera);
        return renderLocalShadowMaps(light);
    }

    void bindEnvironment(const EnvironmentState& environment)
    {
        const bool valid = environment.valid;
        setVec3(main_uniforms.environment_average, valid ? environment.average_color : Vec3{}); setFloat(main_uniforms.environment_intensity, valid ? environment.intensity : 0.0f);
        setFloat(main_uniforms.environment_rotation, environment.rotation_degrees * (kPi / 180.0f)); setVec3(main_uniforms.ambient_color, valid ? environment.ambient_color : Vec3{});
        setFloat(main_uniforms.ambient_intensity, valid ? environment.ambient_intensity : 0.0f); setInt(main_uniforms.fog_mode, valid ? static_cast<int>(environment.fog) : 0);
        setVec3(main_uniforms.fog_color, valid ? environment.fog_color : Vec3{}); setFloat(main_uniforms.fog_density, valid ? environment.fog_density : 0.0f);
        setFloat(main_uniforms.fog_start, valid ? environment.fog_start : 0.0f); setFloat(main_uniforms.fog_end, valid ? environment.fog_end : 1.0f);
        bool has = false;
        if (valid && environment_specular_texture_supported && environment.texture != Models::INVALID_TEXTURE) {
            const GLuint texture = static_cast<GLuint>(textureFor(environment.texture));
            if (texture != 0u) { GLModern.glActiveTexture(GL_TEXTURE0 + kEnvironmentTextureUnit); glBindTexture(GL_TEXTURE_2D, texture); has = true; }
        }
        setInt(main_uniforms.has_environment_texture, has ? 1 : 0); GLModern.glActiveTexture(GL_TEXTURE0);
    }

    void bindGlobalState(const Systems::Scene::LightState& light, const GlobalIllumination::Field *gi,
        const Systems::CameraState& camera, const EnvironmentState& environment)
    {
        int light_type = 0;
        if (light.valid) light_type = light.light.type == LightType::Point ? 1 : (light.light.type == LightType::Directional ? 2 : 3);
        setInt(main_uniforms.light_type, light_type); setVec3(main_uniforms.camera_position, camera.position);
        setVec3(main_uniforms.light_position, light.valid ? light.transform.position : Vec3{}); setVec3(main_uniforms.light_direction, lightDirection(light));
        setVec3(main_uniforms.light_color, light.valid ? light.light.color : Vec3{}); setFloat(main_uniforms.light_intensity, light.valid ? std::max(light.light.intensity,0.0f) : 0.0f);
        setFloat(main_uniforms.light_range, light.valid ? std::max(light.light.range,0.0f) : 0.0f);
        const float inner = light.valid ? std::clamp(light.light.inner_cone_degrees,0.0f,89.9f) : 0.0f;
        const float outer = light.valid ? std::clamp(std::max(light.light.outer_cone_degrees,inner),inner,89.9f) : 0.0f;
        setFloat(main_uniforms.spot_inner_cos, std::cos(inner * (kPi/180.0f))); setFloat(main_uniforms.spot_outer_cos, std::cos(outer * (kPi/180.0f)));
        const bool has_gi = gi_textures.bind(gi, kGiTextureUnit); setInt(main_uniforms.has_gi, has_gi ? 1 : 0);
        if (has_gi && gi) {
            setVec3(main_uniforms.gi_minimum, gi->minimum); setVec3(main_uniforms.gi_maximum, gi->maximum); setFloat(main_uniforms.gi_intensity, std::max(gi->intensity,0.0f));
        } else setFloat(main_uniforms.gi_intensity,0.0f);
        const bool has_shadow = shadow_valid && light_type != 0; setInt(main_uniforms.has_shadow, has_shadow ? 1 : 0);
        setFloat(main_uniforms.shadow_far, has_shadow ? shadow_far : 1.0f); setFloat(main_uniforms.shadow_texel, has_shadow ? 1.0f/static_cast<float>(shadow_target.size()) : 0.0f);
        if (has_shadow) { shadow_target.bind(kShadowTextureUnit); for (int i=0;i<6;++i) setMatrix(main_uniforms.shadow_matrix[i],shadow_matrices[static_cast<std::size_t>(i)]); }
        bindEnvironment(environment); GLModern.glActiveTexture(GL_TEXTURE0);
    }

    void renderSky(const EnvironmentState& environment, const Systems::CameraState& camera)
    {
        if (!environment.valid || !camera.valid) return;
        const GLuint texture = static_cast<GLuint>(textureFor(environment.texture));
        const Systems::CameraState previous = history_valid && previous_camera.valid ? previous_camera : camera;
        const float viewport_aspect = static_cast<float>(width) / static_cast<float>(height);
        const Trace::CameraProjectionEncoding projection =
            Trace::cameraProjectionEncoding(camera, viewport_aspect);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); sky_program.use();
        setInt(sky_uniforms.has_environment_texture, texture != 0u ? 1 : 0); setVec3(sky_uniforms.sky_color, environment.sky_color);
        setFloat(sky_uniforms.environment_intensity, environment.intensity); setFloat(sky_uniforms.environment_rotation, environment.rotation_degrees * (kPi/180.0f));
        setVec3(sky_uniforms.camera_forward,camera.forward); setVec3(sky_uniforms.camera_right,camera.right); setVec3(sky_uniforms.camera_up,camera.up);
        setVec3(sky_uniforms.previous_camera_forward,previous.forward); setVec3(sky_uniforms.previous_camera_right,previous.right); setVec3(sky_uniforms.previous_camera_up,previous.up);
        setFloat(sky_uniforms.tan_half_fov,projection.scale); setFloat(sky_uniforms.aspect,projection.aspect);
        GLModern.glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture != 0u ? texture : fallbackTexture());
        glBegin(GL_TRIANGLES); glVertex2f(-1,-1); glVertex2f(3,-1); glVertex2f(-1,3); glEnd();
        Systems::OpenGL::unbindProgram(); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
    }

    void updateViewportVisibility(const Ecs::World& world)
    {
        viewport_visible.clear(); viewport_filter_valid = false;
        if (!settings.viewport_culling) return;
        const Visibility::Result visibility = Visibility::system().evaluate(world,width,height);
        if (!visibility.frustum.valid) return;
        viewport_visible.insert(visibility.visible.begin(),visibility.visible.end()); viewport_filter_valid = true;
    }

    bool draw(const Ecs::World& world, const GlobalIllumination::Field *gi)
    {
        Systems::Scene::collectRenderItems(world,render_items); updateViewportVisibility(world);
        const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
        if (!camera.valid) { applyClearColor(); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); return true; }
        const Systems::Scene::LightState light = Systems::Scene::lightState(world); renderShadowMaps(light,camera);
        const float viewport_aspect = static_cast<float>(width)/static_cast<float>(height);
        const Math::Mat4 projection = cameraProjectionMatrix(camera,viewport_aspect);
        const Math::Mat4 view = cameraView(camera);
        current_view_projection = Math::multiply(projection,view);
        if (!history_valid) previous_view_projection = current_view_projection;
        const EnvironmentState environment = environmentState(world); renderSky(environment,camera);
        glMatrixMode(GL_PROJECTION); glLoadMatrixf(projection.data()); glMatrixMode(GL_MODELVIEW); glLoadMatrixf(view.data());
        glDisable(GL_LIGHTING); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); main_program.use();
        bindGlobalState(light,gi,camera,environment); drawGeometry(false); Systems::OpenGL::unbindProgram();
        GLModern.glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,0u); glDisable(GL_BLEND);
        previous_view_projection = current_view_projection; previous_camera = camera; history_valid = true;
        return true;
    }
};

Rasterizer::Rasterizer() : impl_(new Impl()) {}
Rasterizer::~Rasterizer() { shutdown(); delete impl_; }

bool Rasterizer::init()
{
    if (impl_->initialized) return true;
    if (glGetString(GL_VERSION) == nullptr) { std::fprintf(stderr,"[Rasterizer]: OpenGL initialization failed: no current context\n"); return false; }
    if (lwcglLoadModernGL() != 0 || !GL20.glCreateShader || !GLModern.glTexImage3D) {
        std::fprintf(stderr,"[Rasterizer]: OpenGL 2.1 shader/3D-texture support unavailable: %s\n",lwcglModernGLMissingFunction()?lwcglModernGLMissingFunction():"unknown"); return false;
    }
    GLint texture_units=0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&texture_units);
    if (texture_units < 16) { std::fprintf(stderr,"[Rasterizer]: at least 16 fragment texture units are required\n"); return false; }
    impl_->environment_specular_texture_supported = texture_units > kEnvironmentTextureUnit;
    if (!impl_->createPrograms()) { impl_->destroyPrograms(); return false; }
    if (impl_->fallbackTexture() == 0u) { impl_->destroyPrograms(); return false; }
    impl_->shadow_target.loadFramebufferApi();
    impl_->frame_target.resize(impl_->width,impl_->height);
    if (!impl_->frame_target.init()) { std::fprintf(stderr,"[Rasterizer]: frame target unavailable\n"); impl_->destroyPrograms(); return false; }
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glDisable(GL_LIGHTING); glShadeModel(GL_SMOOTH);
    impl_->applyClearColor(); glViewport(0,0,impl_->width,impl_->height); impl_->initialized=true;
    std::fprintf(stderr,"[Rasterizer]: OpenGL PBR backend active\n"); return true;
}

void Rasterizer::resize(int width,int height)
{
    impl_->width=std::max(width,1); impl_->height=std::max(height,1);
    if (impl_->initialized) { impl_->frame_target.resize(impl_->width,impl_->height); glViewport(0,0,impl_->width,impl_->height); impl_->history_valid=false; }
}

bool Rasterizer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_->initialized) return false;
    if (!impl_->frame_target.begin()) return false;
    bool ok=true;
    if (!impl_->settings.enabled) { impl_->applyClearColor(); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); }
    else ok=impl_->draw(world,output.global_illumination);
    if (!ok) return false;
    output.api=Internal::GraphicsApi::OpenGL;
    output.width=impl_->width; output.height=impl_->height;
    impl_->frame_target.finish(output);
    return true;
}
bool Rasterizer::compose(Internal::FrameOutput& output) { return impl_ && impl_->frame_target.compose(output); }
void Rasterizer::present(Internal::FrameOutput& output) { (void)output; }

void Rasterizer::shutdown()
{
    if (!impl_ || !impl_->initialized) return;
    Internal::shutdownFonts(Internal::GraphicsApi::OpenGL); impl_->frame_target.shutdown(); impl_->clearTextures(); impl_->gi_textures.clear(); impl_->clearShadowTextures(); impl_->destroyPrograms();
    impl_->render_items.clear(); impl_->viewport_visible.clear(); impl_->previous_models.clear(); impl_->previous_model_valid.clear(); impl_->viewport_filter_valid=false; impl_->history_valid=false; impl_->initialized=false;
}

bool Rasterizer::initialized() const { return impl_ && impl_->initialized; }
bool Rasterizer::enabled() const { return impl_ && impl_->settings.enabled; }
void Rasterizer::setEnabled(bool enabled) { if (impl_) { impl_->settings.enabled=enabled; impl_->history_valid=false; } }
void Rasterizer::setViewportCulling(bool value) { if (impl_) impl_->settings.viewport_culling=value; }
void Rasterizer::setShadowResolution(int value) { if (impl_) { impl_->settings.shadow_resolution=value; impl_->shadow_valid=false; } }
void Rasterizer::setFallbackShadowResolution(int value) { if (impl_) { impl_->settings.fallback_shadow_resolution=value; impl_->shadow_valid=false; } }
void Rasterizer::setMinimumShadowResolution(int value) { if (impl_) { impl_->settings.minimum_shadow_resolution=value; impl_->shadow_valid=false; } }
void Rasterizer::setShadowNearPlane(float value) { if (impl_) { impl_->settings.shadow_near_plane=value; impl_->shadow_valid=false; } }
void Rasterizer::setShadowFarScale(float value) { if (impl_) { impl_->settings.shadow_far_scale=value; impl_->shadow_valid=false; } }
void Rasterizer::setDirectionalShadowDistance(float value) { if (impl_) { impl_->settings.directional_shadow_distance=std::max(value,1.0f); impl_->shadow_valid=false; } }
void Rasterizer::setClearColor(Vec4 value) { if (impl_) impl_->settings.clear_color=value; }
bool Rasterizer::viewportCulling() const { return impl_ && impl_->settings.viewport_culling; }
int Rasterizer::shadowResolution() const { return impl_?impl_->settings.shadow_resolution:0; }
int Rasterizer::fallbackShadowResolution() const { return impl_?impl_->settings.fallback_shadow_resolution:0; }
int Rasterizer::minimumShadowResolution() const { return impl_?impl_->settings.minimum_shadow_resolution:0; }
float Rasterizer::shadowNearPlane() const { return impl_?impl_->settings.shadow_near_plane:0.0f; }
float Rasterizer::shadowFarScale() const { return impl_?impl_->settings.shadow_far_scale:0.0f; }
float Rasterizer::directionalShadowDistance() const { return impl_?impl_->settings.directional_shadow_distance:0.0f; }
Vec4 Rasterizer::clearColor() const { return impl_?impl_->settings.clear_color:Vec4{}; }
RasterizerSettings& Rasterizer::settings() { return impl_->settings; }
const RasterizerSettings& Rasterizer::settings() const { return impl_->settings; }

} // namespace Renderer
