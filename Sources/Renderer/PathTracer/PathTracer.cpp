#include "Renderer/PathTracer/PathTracer.hpp"

#include "Animation/Animation.hpp"
#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/GlobalIlluminationOpenGL.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/PathTracer/PathTracerShaders.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <unordered_map>
#include <vector>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer {
namespace {

using Mat4 = Math::Mat4;
using Vec3f = Vec3;
using Math::cross;
using Math::inverseModelMatrix;
using Math::modelMatrix;
using Math::normalize;
using Math::transformNormal;
using Math::transformPoint;

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kLeafBit = 0x80000000u;
constexpr std::uint32_t kLeafSize = 8u;
constexpr std::size_t kMaximumTriangles = 1000000u;
constexpr std::size_t kMaximumTextureSlots = 16u;

Vec3f subtract(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f minVec(const Vec3f& a, const Vec3f& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3f maxVec(const Vec3f& a, const Vec3f& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

float component(const Vec3f& value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hashValue(hash, static_cast<std::uint32_t>(value));
    hashValue(hash, static_cast<std::uint32_t>(value >> 32u));
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

void hashVec3(std::uint64_t& hash, const Vec3& value)
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

std::uint64_t globalIlluminationSignature(const GlobalIllumination::Field *field)
{
    if (!field || !field->valid()) return 0u;
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, field->revision);
    hashFloat(hash, field->intensity);
    return hash;
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
    std::fprintf(stderr, "[PathTracer]: shader compile failed: %s\n", log.data());
    GL20.glDeleteShader(shader);
    return 0u;
}

GLuint linkProgram(std::initializer_list<GLuint> shaders)
{
    const GLuint program = GL20.glCreateProgram();
    if (program == 0u) return 0u;

    for (GLuint shader : shaders) GL20.glAttachShader(program, shader);
    GL20.glLinkProgram(program);

    GLint status = 0;
    GL20.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        for (GLuint shader : shaders) GL20.glDetachShader(program, shader);
        return program;
    }

    GLint length = 0;
    GL20.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetProgramInfoLog(program, length, nullptr, log.data());
    std::fprintf(stderr, "[PathTracer]: program link failed: %s\n", log.data());
    GL20.glDeleteProgram(program);
    return 0u;
}

GLuint createComputeProgram(const char *source)
{
    const GLuint shader = compileShader(GL_COMPUTE_SHADER, source);
    if (shader == 0u) return 0u;
    const GLuint program = linkProgram({shader});
    GL20.glDeleteShader(shader);
    return program;
}

GLuint createGraphicsProgram(const char *vertex_source, const char *fragment_source)
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertex_source);
    if (vertex == 0u) return 0u;

    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment == 0u) {
        GL20.glDeleteShader(vertex);
        return 0u;
    }

    const GLuint program = linkProgram({vertex, fragment});
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);
    return program;
}

GLint uniformLocation(GLuint program, const char *name)
{
    if (!GL20.glGetUniformLocation) return -1;
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

void setVec2(GLint location, float x, float y)
{
    if (location >= 0) GL20.glUniform2f(location, x, y);
}

void setVec3(GLint location, const Vec3f& value)
{
    if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z);
}

} // namespace

struct PathTracer::Impl {
    struct alignas(16) GpuNode {
        float min_x = 0.0f;
        float min_y = 0.0f;
        float min_z = 0.0f;
        std::uint32_t first = 0u;
        float max_x = 0.0f;
        float max_y = 0.0f;
        float max_z = 0.0f;
        std::uint32_t meta = 0u;
        std::array<std::uint32_t, 4> extra{};
    };

    struct alignas(16) GpuTriangle {
        std::array<float, 4> p0{};
        std::array<float, 4> p1{};
        std::array<float, 4> p2{};
        std::array<float, 4> n0{};
        std::array<float, 4> n1{};
        std::array<float, 4> n2{};
        std::array<float, 4> uv01{};
        std::array<float, 4> uv2{};
    };

    struct alignas(16) GpuMaterial {
        std::array<float, 4> base_color {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<std::int32_t, 4> data {-1, 0, 0, 0};
    };

    struct CameraState {
        bool valid = false;
        Vec3f position{};
        Vec3f forward {0.0f, 0.0f, -1.0f};
        Vec3f right {1.0f, 0.0f, 0.0f};
        Vec3f up {0.0f, 1.0f, 0.0f};
        float fov_degrees = 60.0f;
    };

    struct LightState {
        bool valid = false;
        Vec3f position{};
        Vec3f color {1.0f, 1.0f, 1.0f};
        float intensity = 0.0f;
    };

    struct TraceUniformLocations {
        GLint resolution = -1;
        GLint camera_position = -1;
        GLint camera_forward = -1;
        GLint camera_right = -1;
        GLint camera_up = -1;
        GLint tan_half_fov = -1;
        GLint aspect = -1;
        GLint node_count = -1;
        GLint triangle_count = -1;
        GLint material_count = -1;
        GLint samples_this_frame = -1;
        GLint sample_base = -1;
        GLint frame_index = -1;
        GLint reset_accumulation = -1;
        GLint camera_moving = -1;
        GLint has_light = -1;
        GLint light_position = -1;
        GLint light_color = -1;
        GLint light_intensity = -1;
        std::array<GLint, kMaximumTextureSlots> textures{};
    };

    struct PresentUniformLocations {
        GLint accumulation = -1;
        GLint phase_count = -1;
        GLint camera_moving = -1;
        GLint exposure = -1;
    };

    PathTracerSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    std::uint32_t sample_count = 0u;
    std::uint32_t frame_index = 0u;
    std::uint32_t phase_count = 0u;
    bool reset_pending = true;
    bool camera_moving = false;
    bool was_camera_moving = false;
    std::uint64_t scene_signature = 0u;
    std::uint64_t camera_signature = 0u;
    std::uint64_t light_signature = 0u;
    std::uint64_t gi_signature = 0u;
    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();

    GLuint trace_program = 0u;
    GLuint present_program = 0u;
    GLuint accumulation = 0u;
    GLuint primary_depth = 0u;
    GLuint node_buffer = 0u;
    GLuint triangle_buffer = 0u;
    GLuint material_buffer = 0u;

    TraceUniformLocations trace_uniforms{};
    PresentUniformLocations present_uniforms{};
    bool texture_bindings_dirty = true;
    bool buffer_bindings_dirty = true;

    std::unordered_map<std::uint32_t, GLuint> texture_cache;
    std::vector<GpuNode> gpu_nodes;
    std::vector<GpuTriangle> gpu_triangles;
    std::vector<GpuMaterial> gpu_materials;
    std::vector<Scene::RenderItem> render_items;
    std::array<GLuint, kMaximumTextureSlots> texture_slots{};
    std::size_t texture_slot_count = 0u;

    bool active() const
    {
        return initialized && settings.enabled;
    }

    void resetAccumulation()
    {
        sample_count = 0u;
        reset_pending = true;
    }

    void resetFrameHistory()
    {
        sample_count = 0u;
        frame_index = 0u;
        phase_count = 0u;
        reset_pending = true;
        camera_moving = false;
        was_camera_moving = false;
    }

    void updateTraceResolution()
    {
        const int divisor = std::clamp(settings.resolution_divisor, 1, 4);
        trace_width = std::max(width / divisor, 1);
        trace_height = std::max(height / divisor, 1);
    }

    bool createFloatTexture(GLuint& target, GLint filter)
    {
        GLuint texture = 0u;
        glGenTextures(1, &texture);
        if (texture == 0u) return false;

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA32F,
            trace_width,
            trace_height,
            0,
            GL_RGBA,
            GL_FLOAT,
            nullptr
        );
        target = texture;
        return true;
    }

    bool createTraceTargets()
    {
        if (!createFloatTexture(accumulation, GL_LINEAR)) return false;
        if (!createFloatTexture(primary_depth, GL_NEAREST)) {
            glDeleteTextures(1, &accumulation);
            accumulation = 0u;
            return false;
        }
        resetFrameHistory();
        return true;
    }

    void destroyTraceTargets()
    {
        if (accumulation != 0u) glDeleteTextures(1, &accumulation);
        if (primary_depth != 0u) glDeleteTextures(1, &primary_depth);
        accumulation = 0u;
        primary_depth = 0u;
        resetFrameHistory();
    }

    void cacheUniformLocations()
    {
        trace_uniforms.resolution = uniformLocation(trace_program, "uResolution");
        trace_uniforms.camera_position = uniformLocation(trace_program, "uCameraPosition");
        trace_uniforms.camera_forward = uniformLocation(trace_program, "uCameraForward");
        trace_uniforms.camera_right = uniformLocation(trace_program, "uCameraRight");
        trace_uniforms.camera_up = uniformLocation(trace_program, "uCameraUp");
        trace_uniforms.tan_half_fov = uniformLocation(trace_program, "uTanHalfFov");
        trace_uniforms.aspect = uniformLocation(trace_program, "uAspect");
        trace_uniforms.node_count = uniformLocation(trace_program, "uNodeCount");
        trace_uniforms.triangle_count = uniformLocation(trace_program, "uTriangleCount");
        trace_uniforms.material_count = uniformLocation(trace_program, "uMaterialCount");
        trace_uniforms.samples_this_frame = uniformLocation(trace_program, "uSamplesThisFrame");
        trace_uniforms.sample_base = uniformLocation(trace_program, "uSampleBase");
        trace_uniforms.frame_index = uniformLocation(trace_program, "uFrameIndex");
        trace_uniforms.reset_accumulation = uniformLocation(trace_program, "uResetAccumulation");
        trace_uniforms.camera_moving = uniformLocation(trace_program, "uCameraMoving");
        trace_uniforms.has_light = uniformLocation(trace_program, "uHasLight");
        trace_uniforms.light_position = uniformLocation(trace_program, "uLightPosition");
        trace_uniforms.light_color = uniformLocation(trace_program, "uLightColor");
        trace_uniforms.light_intensity = uniformLocation(trace_program, "uLightIntensity");

        GL20.glUseProgram(trace_program);
        for (std::size_t slot = 0u; slot < trace_uniforms.textures.size(); ++slot) {
            char name[32]{};
            std::snprintf(name, sizeof(name), "uTexture%zu", slot);
            trace_uniforms.textures[slot] = uniformLocation(trace_program, name);
            setInt(trace_uniforms.textures[slot], static_cast<int>(slot));
        }

        present_uniforms.accumulation = uniformLocation(present_program, "uAccumulation");
        present_uniforms.phase_count = uniformLocation(present_program, "uPhaseCount");
        present_uniforms.camera_moving = uniformLocation(present_program, "uCameraMoving");
        present_uniforms.exposure = uniformLocation(present_program, "uExposure");
        GL20.glUseProgram(present_program);
        setInt(present_uniforms.accumulation, 0);
        GL20.glUseProgram(0u);
    }

    bool createPrograms()
    {
        trace_program = createComputeProgram(PathTracerShaders::trace);
        present_program = createGraphicsProgram(
            PathTracerShaders::present_vertex,
            PathTracerShaders::present_fragment
        );
        if (trace_program == 0u || present_program == 0u) return false;
        cacheUniformLocations();
        return true;
    }

    void destroyPrograms()
    {
        if (!GL20.glDeleteProgram) return;
        if (trace_program != 0u) GL20.glDeleteProgram(trace_program);
        if (present_program != 0u) GL20.glDeleteProgram(present_program);
        trace_program = 0u;
        present_program = 0u;
        trace_uniforms = {};
        present_uniforms = {};
    }

    bool ensureBuffer(GLuint& buffer)
    {
        if (buffer != 0u) return true;
        GL15.glGenBuffers(1, &buffer);
        return buffer != 0u;
    }

    bool uploadBuffer(GLuint& buffer, const void *data, std::size_t bytes, GLenum usage)
    {
        if (!ensureBuffer(buffer)) return false;
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        const std::size_t safe_bytes = std::max<std::size_t>(bytes, 16u);
        GL15.glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLsizeiptr>(safe_bytes),
            bytes == 0u ? nullptr : data,
            usage
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    void destroyBuffers()
    {
        if (!GL15.glDeleteBuffers) return;
        GLuint buffers[] = {node_buffer, triangle_buffer, material_buffer};
        for (GLuint buffer : buffers) {
            if (buffer != 0u) GL15.glDeleteBuffers(1, &buffer);
        }
        node_buffer = 0u;
        triangle_buffer = 0u;
        material_buffer = 0u;
        buffer_bindings_dirty = true;
    }

    GLuint textureFor(std::uint32_t handle)
    {
        if (handle == Models::INVALID_TEXTURE) return 0u;
        const auto found = texture_cache.find(handle);
        if (found != texture_cache.end()) return found->second;

        const Models::TextureAsset *asset = Models::texture(handle);
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
            return 0u;
        }

        GLuint texture = 0u;
        glGenTextures(1, &texture);
        if (texture == 0u) return 0u;

        glBindTexture(GL_TEXTURE_2D, texture);
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

        texture_cache.emplace(handle, texture);
        return texture;
    }

    static Vec3f triangleCentroid(const GpuTriangle& triangle)
    {
        return {
            (triangle.p0[0] + triangle.p1[0] + triangle.p2[0]) / 3.0f,
            (triangle.p0[1] + triangle.p1[1] + triangle.p2[1]) / 3.0f,
            (triangle.p0[2] + triangle.p1[2] + triangle.p2[2]) / 3.0f,
        };
    }

    std::uint32_t buildNode(std::uint32_t start, std::uint32_t count)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3f bounds_min{infinity, infinity, infinity};
        Vec3f bounds_max{-infinity, -infinity, -infinity};
        Vec3f centroid_min{infinity, infinity, infinity};
        Vec3f centroid_max{-infinity, -infinity, -infinity};

        for (std::uint32_t index = 0u; index < count; ++index) {
            const GpuTriangle& triangle = gpu_triangles[start + index];
            const Vec3f p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
            const Vec3f p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
            const Vec3f p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
            bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
            bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
            const Vec3f centroid = triangleCentroid(triangle);
            centroid_min = minVec(centroid_min, centroid);
            centroid_max = maxVec(centroid_max, centroid);
        }

        const std::uint32_t node_index = static_cast<std::uint32_t>(gpu_nodes.size());
        GpuNode node;
        node.min_x = bounds_min.x;
        node.min_y = bounds_min.y;
        node.min_z = bounds_min.z;
        node.max_x = bounds_max.x;
        node.max_y = bounds_max.y;
        node.max_z = bounds_max.z;
        gpu_nodes.push_back(node);

        const Vec3f extent = subtract(centroid_max, centroid_min);
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > component(extent, axis)) axis = 2;

        if (count <= kLeafSize || component(extent, axis) <= 1.0e-6f) {
            gpu_nodes[node_index].first = start;
            gpu_nodes[node_index].meta = kLeafBit | count;
            gpu_nodes[node_index].extra[0] = static_cast<std::uint32_t>(gpu_nodes.size());
            return node_index;
        }

        const std::uint32_t left_count = count / 2u;
        const std::uint32_t middle = start + left_count;
        std::nth_element(
            gpu_triangles.begin() + start,
            gpu_triangles.begin() + middle,
            gpu_triangles.begin() + start + count,
            [axis](const GpuTriangle& a, const GpuTriangle& b) {
                return component(triangleCentroid(a), axis) < component(triangleCentroid(b), axis);
            }
        );

        const std::uint32_t left = buildNode(start, left_count);
        const std::uint32_t right = buildNode(middle, count - left_count);
        gpu_nodes[node_index].first = left;
        gpu_nodes[node_index].meta = right;
        gpu_nodes[node_index].extra[0] = static_cast<std::uint32_t>(gpu_nodes.size());
        return node_index;
    }

    std::uint64_t sceneSignature(
        const Ecs::World& world,
        const std::vector<Scene::RenderItem>& items
    ) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const Scene::RenderItem& item : items) {
            hashValue(hash, item.entity);
            hashValue(hash, item.mesh_component->mesh);
            hashValue(hash, item.mesh_component->material);
            hashTransform(hash, *item.transform);

            const Animation::SkinBindingComponent *binding =
                world.get<Animation::SkinBindingComponent>(item.entity);
            if (binding && binding->animator != Ecs::INVALID_ENTITY) {
                const Animation::AnimatorComponent *animator =
                    world.get<Animation::AnimatorComponent>(binding->animator);
                if (animator) {
                    hashValue(hash, binding->animator);
                    hashValue(hash, animator->pose.revision);
                }
            }
        }
        return hash;
    }

    CameraState cameraState(const Scene::CameraState& source) const
    {
        CameraState state;
        if (!source.valid) return state;

        const Vec3 forward = Camera::flightDirection(
            source.transform.rotation.y,
            source.transform.rotation.x
        );
        const Vec3 right = Camera::strafeDirection(source.transform.rotation.y);

        state.valid = true;
        state.position = source.transform.position;
        state.forward = normalize(forward);
        state.right = normalize(right);
        state.up = normalize(cross(state.right, state.forward));
        state.fov_degrees = std::clamp(source.fov_degrees, 1.0f, 179.0f);
        return state;
    }

    std::uint64_t cameraSignature(const CameraState& camera) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        hashValue(hash, camera.valid ? 1u : 0u);
        hashFloat(hash, camera.position.x);
        hashFloat(hash, camera.position.y);
        hashFloat(hash, camera.position.z);
        hashFloat(hash, camera.forward.x);
        hashFloat(hash, camera.forward.y);
        hashFloat(hash, camera.forward.z);
        hashFloat(hash, camera.fov_degrees);
        return hash;
    }

    LightState lightState(const Scene::LightState& source) const
    {
        LightState result;
        if (!source.valid || source.light.type != LightType::Point) return result;

        result.valid = true;
        result.position = source.transform.position;
        result.color = source.light.color;
        result.intensity = std::max(source.light.intensity, 0.0f);
        return result;
    }

    std::uint64_t lightSignature(const LightState& light) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        hashValue(hash, light.valid ? 1u : 0u);
        hashFloat(hash, light.position.x);
        hashFloat(hash, light.position.y);
        hashFloat(hash, light.position.z);
        hashFloat(hash, light.color.x);
        hashFloat(hash, light.color.y);
        hashFloat(hash, light.color.z);
        hashFloat(hash, light.intensity);
        return hash;
    }

    bool syncScene(
        const Ecs::World& world,
        const std::vector<Scene::RenderItem>& items
    )
    {
        gpu_nodes.clear();
        gpu_triangles.clear();
        gpu_materials.clear();
        texture_slots.fill(0u);
        texture_slot_count = 0u;

        std::unordered_map<std::uint32_t, std::uint32_t> material_indices;
        std::unordered_map<std::uint32_t, int> texture_indices;

        auto materialIndex = [&](std::uint32_t handle) -> std::uint32_t {
            const auto found = material_indices.find(handle);
            if (found != material_indices.end()) return found->second;

            GpuMaterial gpu_material;
            const Models::MaterialData *material = Models::material(handle);
            if (material) {
                gpu_material.base_color = {
                    material->color.x,
                    material->color.y,
                    material->color.z,
                    std::clamp(material->opacity, 0.0f, 1.0f),
                };

                if (material->diffuse_texture != Models::INVALID_TEXTURE) {
                    const auto texture_found = texture_indices.find(material->diffuse_texture);
                    int slot = -1;
                    if (texture_found != texture_indices.end()) {
                        slot = texture_found->second;
                    } else if (texture_slot_count < texture_slots.size()) {
                        const GLuint texture = textureFor(material->diffuse_texture);
                        if (texture != 0u) {
                            slot = static_cast<int>(texture_slot_count);
                            texture_slots[texture_slot_count++] = texture;
                            texture_indices.emplace(material->diffuse_texture, slot);
                        }
                    }
                    gpu_material.data[0] = slot;
                }
            }

            const std::uint32_t index = static_cast<std::uint32_t>(gpu_materials.size());
            gpu_materials.push_back(gpu_material);
            material_indices.emplace(handle, index);
            return index;
        };

        gpu_triangles.reserve(262144u);

        for (const Scene::RenderItem& item : items) {
            if (gpu_triangles.size() >= kMaximumTriangles) break;

            const Ecs::Entity entity = item.entity;
            const MeshComponent *mesh_component = item.mesh_component;
            const Transform *transform = item.transform;
            const Models::MeshData *mesh = item.mesh;
            if (!mesh || mesh->indices.size() < 3u || mesh->vertices.empty()) continue;

            const Models::MaterialData *material = item.material;
            if (material && material->opacity < 0.5f) continue;

            const std::uint32_t material_index = materialIndex(mesh_component->material);
            const Mat4 model = modelMatrix(*transform);
            const Mat4 world_to_object = inverseModelMatrix(*transform);

            const Animation::Pose *pose = nullptr;
            const Animation::SkinBindingComponent *binding =
                world.get<Animation::SkinBindingComponent>(entity);
            if (binding && binding->animator != Ecs::INVALID_ENTITY) {
                const Animation::AnimatorComponent *animator =
                    world.get<Animation::AnimatorComponent>(binding->animator);
                if (animator && !animator->pose.skin.empty()) pose = &animator->pose;
            }

            std::vector<Vec3f> positions(mesh->vertices.size());
            std::vector<Vec3f> normals(mesh->vertices.size());

            for (std::size_t index = 0u; index < mesh->vertices.size(); ++index) {
                const Models::Vertex& vertex = mesh->vertices[index];
                Vec3f local_position{vertex.position.x, vertex.position.y, vertex.position.z};
                Vec3f local_normal{vertex.normal.x, vertex.normal.y, vertex.normal.z};

                if (pose) {
                    Animation::Vec3 skinned_position{};
                    Animation::Vec3 skinned_normal{};
                    Animation::skinVertex(
                        *pose,
                        vertex.skin,
                        {local_position.x, local_position.y, local_position.z},
                        {local_normal.x, local_normal.y, local_normal.z},
                        &skinned_position,
                        &skinned_normal
                    );
                    local_position = {skinned_position.x, skinned_position.y, skinned_position.z};
                    local_normal = {skinned_normal.x, skinned_normal.y, skinned_normal.z};
                }

                positions[index] = transformPoint(model, local_position);
                normals[index] = transformNormal(world_to_object, local_normal);
            }

            const std::size_t triangle_count = mesh->indices.size() / 3u;
            for (std::size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
                if (gpu_triangles.size() >= kMaximumTriangles) break;

                const std::size_t offset = triangle_index * 3u;
                const std::uint32_t i0 = mesh->indices[offset + 0u];
                const std::uint32_t i1 = mesh->indices[offset + 1u];
                const std::uint32_t i2 = mesh->indices[offset + 2u];
                if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;

                const Models::Vertex& v0 = mesh->vertices[i0];
                const Models::Vertex& v1 = mesh->vertices[i1];
                const Models::Vertex& v2 = mesh->vertices[i2];
                const Vec3f& p0 = positions[i0];
                const Vec3f& p1 = positions[i1];
                const Vec3f& p2 = positions[i2];
                const Vec3f& n0 = normals[i0];
                const Vec3f& n1 = normals[i1];
                const Vec3f& n2 = normals[i2];

                GpuTriangle triangle;
                triangle.p0 = {p0.x, p0.y, p0.z, std::bit_cast<float>(material_index)};
                triangle.p1 = {p1.x, p1.y, p1.z, 0.0f};
                triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
                triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
                triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
                triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
                triangle.uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
                triangle.uv2 = {v2.uv.x, v2.uv.y, 0.0f, 0.0f};
                gpu_triangles.push_back(triangle);
            }
        }

        if (gpu_materials.empty()) gpu_materials.push_back(GpuMaterial{});

        if (!gpu_triangles.empty()) {
            gpu_nodes.reserve(gpu_triangles.size() * 2u);
            buildNode(0u, static_cast<std::uint32_t>(gpu_triangles.size()));
        }

        const bool uploaded =
            uploadBuffer(
                node_buffer,
                gpu_nodes.data(),
                gpu_nodes.size() * sizeof(GpuNode),
                GL_STATIC_DRAW
            ) &&
            uploadBuffer(
                triangle_buffer,
                gpu_triangles.data(),
                gpu_triangles.size() * sizeof(GpuTriangle),
                GL_STATIC_DRAW
            ) &&
            uploadBuffer(
                material_buffer,
                gpu_materials.data(),
                gpu_materials.size() * sizeof(GpuMaterial),
                GL_STATIC_DRAW
            );

        if (uploaded) {
            buffer_bindings_dirty = true;
            texture_bindings_dirty = true;
            std::fprintf(
                stderr,
                "[PathTracer]: world cache %zu triangles, %zu nodes, %zu materials\n",
                gpu_triangles.size(),
                gpu_nodes.size(),
                gpu_materials.size()
            );
        }

        return uploaded;
    }

    void bindTexturesForDispatch()
    {
        if (texture_bindings_dirty) {
            for (std::size_t slot = 0u; slot < texture_slots.size(); ++slot) {
                GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
                glBindTexture(GL_TEXTURE_2D, texture_slots[slot]);
            }
            texture_bindings_dirty = false;
        } else {
            GLModern.glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture_slots[0]);
        }
        GLModern.glActiveTexture(GL_TEXTURE0);
    }

    void bindBuffersIfDirty()
    {
        if (!buffer_bindings_dirty) return;
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, material_buffer);
        buffer_bindings_dirty = false;
    }

    void dispatch(const CameraState& camera, const LightState& light)
    {
        const int samples = std::clamp(settings.samples_per_frame, 1, 4);
        if (sample_count > 1000000000u - static_cast<std::uint32_t>(samples)) resetAccumulation();
        if (frame_index > 1000000000u) frame_index &= 3u;

        GL20.glUseProgram(trace_program);
        setVec2(
            trace_uniforms.resolution,
            static_cast<float>(trace_width),
            static_cast<float>(trace_height)
        );
        setVec3(trace_uniforms.camera_position, camera.position);
        setVec3(trace_uniforms.camera_forward, camera.forward);
        setVec3(trace_uniforms.camera_right, camera.right);
        setVec3(trace_uniforms.camera_up, camera.up);
        setFloat(trace_uniforms.tan_half_fov, std::tan(camera.fov_degrees * (kPi / 360.0f)));
        setFloat(trace_uniforms.aspect, static_cast<float>(width) / static_cast<float>(height));
        setInt(trace_uniforms.node_count, static_cast<int>(gpu_nodes.size()));
        setInt(trace_uniforms.triangle_count, static_cast<int>(gpu_triangles.size()));
        setInt(trace_uniforms.material_count, static_cast<int>(gpu_materials.size()));
        setInt(trace_uniforms.samples_this_frame, samples);
        setInt(trace_uniforms.sample_base, static_cast<int>(sample_count));
        setInt(trace_uniforms.frame_index, static_cast<int>(frame_index));
        setInt(trace_uniforms.reset_accumulation, reset_pending ? 1 : 0);
        setInt(trace_uniforms.camera_moving, camera_moving ? 1 : 0);
        setInt(trace_uniforms.has_light, light.valid ? 1 : 0);
        setVec3(trace_uniforms.light_position, light.position);
        setVec3(trace_uniforms.light_color, light.color);
        setFloat(trace_uniforms.light_intensity, light.intensity);

        bindTexturesForDispatch();
        bindBuffersIfDirty();
        GL42.glBindImageTexture(0u, accumulation, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        GL42.glBindImageTexture(1u, primary_depth, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

        GL43.glDispatchCompute(
            static_cast<GLuint>((trace_width + 7) / 8),
            static_cast<GLuint>((trace_height + 7) / 8),
            1u
        );
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        GL20.glUseProgram(0u);

        sample_count += static_cast<std::uint32_t>(samples);
        ++frame_index;
        phase_count = std::min<std::uint32_t>(phase_count + 1u, 4u);
        reset_pending = false;
    }

    void compose()
    {
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);

        GL20.glUseProgram(present_program);
        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accumulation);
        setFloat(present_uniforms.phase_count, static_cast<float>(std::max(phase_count, 1u)));
        setInt(present_uniforms.camera_moving, camera_moving ? 1 : 0);
        setFloat(present_uniforms.exposure, settings.exposure);

        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(3.0f, -1.0f);
        glVertex2f(-1.0f, 3.0f);
        glEnd();

        GL20.glUseProgram(0u);
        GLModern.glActiveTexture(GL_TEXTURE0);
    }
};

static_assert(sizeof(PathTracer::Impl::GpuNode) == 48u);
static_assert(sizeof(PathTracer::Impl::GpuTriangle) == 128u);
static_assert(sizeof(PathTracer::Impl::GpuMaterial) == 32u);

PathTracer::PathTracer() : impl_(new Impl) {}

PathTracer::~PathTracer()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool PathTracer::init()
{
    if (impl_->initialized) return true;

    if (!lwcglModernGLAvailable() && lwcglLoadModernGL() != 0) {
        std::fprintf(stderr, "[PathTracer]: modern OpenGL unavailable\n");
        return false;
    }

    const int major = lwcglModernGLMajorVersion();
    const int minor = lwcglModernGLMinorVersion();
    if (
        major < 4 ||
        (major == 4 && minor < 3) ||
        !GL43.glDispatchCompute ||
        !GL42.glBindImageTexture ||
        !GL30.glBindBufferBase)
    {
        std::fprintf(
            stderr,
            "[PathTracer]: OpenGL 4.3 compatibility context required; found %d.%d\n",
            major,
            minor
        );
        return false;
    }

    impl_->updateTraceResolution();
    if (!impl_->createPrograms() || !impl_->createTraceTargets()) {
        shutdown();
        return false;
    }

    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[PathTracer]: OpenGL %d.%d, output %dx%d, trace %dx%d\n",
        major,
        minor,
        impl_->width,
        impl_->height,
        impl_->trace_width,
        impl_->trace_height
    );
    return true;
}

void PathTracer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateTraceResolution();

    if (!impl_->initialized) return;
    if (impl_->trace_width == previous_width && impl_->trace_height == previous_height) {
        impl_->resetAccumulation();
        return;
    }

    impl_->destroyTraceTargets();
    if (!impl_->createTraceTargets()) {
        std::fprintf(stderr, "[PathTracer]: failed to resize trace targets\n");
        shutdown();
    }
}

bool PathTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_->initialized) return false;

    output.api = Internal::GraphicsApi::OpenGL;
    output.depth = Internal::DepthSource::None;
    output.width = impl_->width;
    output.height = impl_->height;
    output.command = nullptr;
    output.depth_texture = nullptr;

    if (!impl_->active()) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return true;
    }

    const int previous_trace_width = impl_->trace_width;
    const int previous_trace_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (
        impl_->trace_width != previous_trace_width ||
        impl_->trace_height != previous_trace_height)
    {
        impl_->destroyTraceTargets();
        if (!impl_->createTraceTargets()) {
            std::fprintf(stderr, "[PathTracer]: failed to recreate trace targets\n");
            shutdown();
            return false;
        }
    }

    const Scene::CameraState scene_camera = Scene::cameraState(world);
    const Impl::CameraState camera = impl_->cameraState(scene_camera);
    if (!camera.valid) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return true;
    }

    const Scene::LightState scene_light = Scene::lightState(world);
    const Impl::LightState light = impl_->lightState(scene_light);
    const std::uint64_t next_camera_signature = impl_->cameraSignature(camera);
    const std::uint64_t next_light_signature = impl_->lightSignature(light);
    const std::uint64_t next_gi_signature = globalIlluminationSignature(output.global_illumination);
    const std::uint64_t next_world_revision = world.changeRevision();

    if (next_world_revision != impl_->world_revision) {
        Scene::collectRenderItems(world, impl_->render_items);
        const std::uint64_t next_scene_signature =
            impl_->sceneSignature(world, impl_->render_items);
        if (next_scene_signature != impl_->scene_signature) {
            if (!impl_->syncScene(world, impl_->render_items)) {
                std::fprintf(stderr, "[PathTracer]: failed to synchronize world cache\n");
                return false;
            }
            impl_->scene_signature = next_scene_signature;
            impl_->resetAccumulation();
        }
        impl_->world_revision = next_world_revision;
    }

    const bool camera_changed = next_camera_signature != impl_->camera_signature;
    impl_->camera_moving = camera_changed;
    if (camera_changed) {
        impl_->camera_signature = next_camera_signature;
        impl_->resetAccumulation();
    } else if (impl_->was_camera_moving) {
        impl_->resetAccumulation();
    }
    impl_->was_camera_moving = camera_changed;

    if (next_light_signature != impl_->light_signature) {
        impl_->light_signature = next_light_signature;
        impl_->resetAccumulation();
    }

    if (next_gi_signature != impl_->gi_signature) {
        impl_->gi_signature = next_gi_signature;
        impl_->resetAccumulation();
    }

    Internal::bindGlobalIlluminationOpenGL(output.global_illumination);
    impl_->dispatch(camera, light);
    impl_->compose();

    output.depth = Internal::DepthSource::LinearTexture;
    output.depth_texture = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(impl_->primary_depth)
    );
    return true;
}

void PathTracer::present(Internal::FrameOutput& output)
{
    (void)output;
}

void PathTracer::shutdown()
{
    if (!impl_) return;

    Internal::shutdownFonts(Internal::GraphicsApi::OpenGL);
    Internal::shutdownGlobalIlluminationOpenGL();
    if (GL20.glUseProgram) GL20.glUseProgram(0u);
    impl_->destroyPrograms();
    impl_->destroyTraceTargets();
    impl_->destroyBuffers();

    for (const auto& [handle, texture] : impl_->texture_cache) {
        (void)handle;
        if (texture != 0u) glDeleteTextures(1, &texture);
    }

    impl_->texture_cache.clear();
    impl_->gpu_nodes.clear();
    impl_->gpu_triangles.clear();
    impl_->gpu_materials.clear();
    impl_->render_items.clear();
    impl_->texture_slots.fill(0u);
    impl_->texture_slot_count = 0u;
    impl_->scene_signature = 0u;
    impl_->camera_signature = 0u;
    impl_->light_signature = 0u;
    impl_->gi_signature = 0u;
    impl_->world_revision = std::numeric_limits<std::uint64_t>::max();
    impl_->texture_bindings_dirty = true;
    impl_->buffer_bindings_dirty = true;
    impl_->initialized = false;
}

bool PathTracer::initialized() const
{
    return impl_->initialized;
}

bool PathTracer::enabled() const
{
    return impl_->settings.enabled;
}

void PathTracer::setEnabled(bool enabled)
{
    impl_->settings.enabled = enabled;
    impl_->resetAccumulation();
}

PathTracerSettings& PathTracer::settings()
{
    return impl_->settings;
}

const PathTracerSettings& PathTracer::settings() const
{
    return impl_->settings;
}

} // namespace Renderer