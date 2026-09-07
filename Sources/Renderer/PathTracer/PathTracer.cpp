#include "Renderer/PathTracer/PathTracer.hpp"

#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/PathTracerGpu.hpp"
#include "Renderer/PathTracer/PathTracerScene.hpp"
#include "Renderer/PathTracer/PrimaryTraceShader.hpp"
#include "Renderer/PathTracer/RadianceCacheShaders.hpp"
#include "Renderer/PathTracer/RestirShaders.hpp"
#include "Renderer/PathTracer/SvgfShaders.hpp"
#include "Renderer/PathTracer/WavefrontRuntimeShaders.hpp"
#include "Renderer/PathTracer/WavefrontShaders.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_DISPATCH_INDIRECT_BUFFER
#define GL_DISPATCH_INDIRECT_BUFFER 0x90EE
#endif

namespace Renderer {
namespace {

using Mat4 = PathTracerGpu::Mat4;

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kMaximumTextureSlots = 16u;

Vec3f scale(const Vec3f& value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(const Vec3f& a, const Vec3f& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f cross(const Vec3f& a, const Vec3f& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3f normalize(const Vec3f& value)
{
    const float squared = dot(value, value);
    if (squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    return scale(value, 1.0f / std::sqrt(squared));
}

Mat4 identityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
            }
        }
    }
    return result;
}

Mat4 viewMatrix(const Vec3f& position, const Vec3f& forward, const Vec3f& right, const Vec3f& up)
{
    Mat4 result = identityMatrix();
    result[0] = right.x;
    result[4] = right.y;
    result[8] = right.z;
    result[1] = up.x;
    result[5] = up.y;
    result[9] = up.z;
    result[2] = -forward.x;
    result[6] = -forward.y;
    result[10] = -forward.z;
    result[12] = -dot(right, position);
    result[13] = -dot(up, position);
    result[14] = dot(forward, position);
    return result;
}

Mat4 perspectiveMatrix(float fov_degrees, float aspect, float near_plane, float far_plane)
{
    const float tangent = std::tan(fov_degrees * (kPi / 360.0f));
    const float f = tangent > 1.0e-8f ? 1.0f / tangent : 1.0f;
    Mat4 result{};
    result[0] = f / std::max(aspect, 1.0e-6f);
    result[5] = f;
    result[10] = (far_plane + near_plane) / (near_plane - far_plane);
    result[11] = -1.0f;
    result[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    return result;
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

GLuint compileShader(GLenum stage, std::initializer_list<const char *> sources)
{
    const GLuint shader = GL20.glCreateShader(stage);
    if (shader == 0u) return 0u;
    GL20.glShaderSource(shader, static_cast<GLsizei>(sources.size()), sources.begin(), nullptr);
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

GLuint createComputeProgram(std::initializer_list<const char *> sources)
{
    const GLuint shader = compileShader(GL_COMPUTE_SHADER, sources);
    if (shader == 0u) return 0u;
    const GLuint program = linkProgram({shader});
    GL20.glDeleteShader(shader);
    return program;
}

GLuint createGraphicsProgram(const char *vertex_source, const char *fragment_source)
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, {vertex_source});
    if (vertex == 0u) return 0u;
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, {fragment_source});
    if (fragment == 0u) {
        GL20.glDeleteShader(vertex);
        return 0u;
    }
    const GLuint program = linkProgram({vertex, fragment});
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);
    return program;
}

void setInt(GLuint program, const char *name, int value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(GLuint program, const char *name, float value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setVec3(GLuint program, const char *name, const Vec3f& value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z);
}

void setMat4(GLuint program, const char *name, const Mat4& value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniformMatrix4fv(location, 1, GL_FALSE, value.data());
}

std::size_t roundUp64(std::size_t value)
{
    return (value + 63u) & ~std::size_t(63u);
}

} // namespace

struct PathTracer::Impl {
    struct CameraState {
        bool valid = false;
        Vec3f position{};
        Vec3f forward {0.0f, 0.0f, -1.0f};
        Vec3f right {1.0f, 0.0f, 0.0f};
        Vec3f up {0.0f, 1.0f, 0.0f};
        float fov_degrees = 60.0f;
        Mat4 view_projection = identityMatrix();
    };

    struct LightState {
        bool valid = false;
        Vec3f position{};
        Vec3f color {1.0f, 1.0f, 1.0f};
        float intensity = 0.0f;
    };

    PathTracerSettings settings{};
    PathTracerScene::SceneCache scene;
    bool initialized = false;
    bool history_valid = false;
    bool cache_needs_clear = true;
    int width = 1;
    int height = 1;
    int render_width = 1;
    int render_height = 1;
    int allocated_width = 0;
    int allocated_height = 0;
    std::size_t bounce_capacity = 64u;
    std::uint32_t frame_index = 0u;
    std::uint64_t light_signature = 0u;
    Mat4 previous_view_projection = identityMatrix();
    Vec3f previous_camera_position{};

    GLuint primary_trace_program = 0u;
    GLuint prepare_dispatch_program = 0u;
    GLuint primary_shade_program = 0u;
    GLuint classify_bounces_program = 0u;
    GLuint prepare_reorder_program = 0u;
    GLuint scatter_bounces_program = 0u;
    GLuint bounce_intersect_program = 0u;
    GLuint bounce_shade_program = 0u;
    GLuint restir_temporal_program = 0u;
    GLuint restir_spatial_program = 0u;
    GLuint compose_program = 0u;
    GLuint temporal_filter_program = 0u;
    GLuint atrous_program = 0u;
    GLuint copy_output_program = 0u;
    GLuint clear_cache_program = 0u;
    GLuint present_program = 0u;

    GLuint node_buffer = 0u;
    GLuint triangle_buffer = 0u;
    GLuint instance_buffer = 0u;
    GLuint material_buffer = 0u;
    GLuint surface_current = 0u;
    GLuint surface_previous = 0u;
    GLuint secondary_surface = 0u;
    GLuint bounce_ray_buffer = 0u;
    GLuint sorted_bounce_ray_buffer = 0u;
    GLuint queue_control_buffer = 0u;
    GLuint reservoir_work_a = 0u;
    GLuint reservoir_work_b = 0u;
    GLuint reservoir_previous = 0u;
    GLuint lighting_current = 0u;
    GLuint lighting_temporal = 0u;
    GLuint lighting_history = 0u;
    GLuint moments_current = 0u;
    GLuint moments_previous = 0u;
    GLuint radiance_cache = 0u;
    GLuint output_texture = 0u;

    std::unordered_map<std::uint32_t, GLuint> texture_cache;
    std::array<GLuint, kMaximumTextureSlots> texture_slots{};

    bool active() const { return initialized && settings.enabled; }

    std::size_t pixelCount() const
    {
        return static_cast<std::size_t>(render_width) * static_cast<std::size_t>(render_height);
    }

    void updateRenderResolution()
    {
        const int divisor = std::clamp(settings.resolution_divisor, 1, 4);
        render_width = std::max(width / divisor, 1);
        render_height = std::max(height / divisor, 1);
        const std::size_t phase_width = static_cast<std::size_t>((render_width + 1) / 2);
        const std::size_t phase_height = static_cast<std::size_t>((render_height + 1) / 2);
        bounce_capacity = std::max<std::size_t>(roundUp64(phase_width * phase_height), 64u);
    }

    bool ensureBuffer(GLuint& buffer, std::size_t bytes, GLenum usage)
    {
        if (buffer == 0u) GL15.glGenBuffers(1, &buffer);
        if (buffer == 0u) return false;
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        GL15.glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLsizeiptr>(std::max<std::size_t>(bytes, 16u)),
            nullptr,
            usage
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    bool uploadBuffer(GLuint& buffer, const void *data, std::size_t bytes, GLenum usage)
    {
        if (buffer == 0u) GL15.glGenBuffers(1, &buffer);
        if (buffer == 0u) return false;
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        GL15.glBufferData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLsizeiptr>(std::max<std::size_t>(bytes, 16u)),
            bytes == 0u ? nullptr : data,
            usage
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    bool updateBufferRange(
        GLuint buffer,
        std::size_t element_size,
        std::size_t first,
        std::size_t count,
        const void *base)
    {
        if (buffer == 0u || count == 0u || !base) return count == 0u;
        const auto *bytes = static_cast<const std::uint8_t *>(base);
        const std::size_t byte_offset = first * element_size;
        const std::size_t byte_count = count * element_size;
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        GL15.glBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            static_cast<LWCGLintptr>(byte_offset),
            static_cast<LWCGLsizeiptr>(byte_count),
            bytes + byte_offset
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    void deleteBuffer(GLuint& buffer)
    {
        if (buffer != 0u && GL15.glDeleteBuffers) GL15.glDeleteBuffers(1, &buffer);
        buffer = 0u;
    }

    bool createOutputTexture()
    {
        if (output_texture == 0u) glGenTextures(1, &output_texture);
        if (output_texture == 0u) return false;
        glBindTexture(GL_TEXTURE_2D, output_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA16F,
            render_width,
            render_height,
            0,
            GL_RGBA,
            GL_FLOAT,
            nullptr
        );
        return true;
    }

    bool allocatePersistentResources()
    {
        if (radiance_cache != 0u) return true;
        return ensureBuffer(
            radiance_cache,
            PathTracerGpu::RADIANCE_CACHE_ENTRIES * sizeof(PathTracerGpu::RadianceCacheEntry),
            GL_DYNAMIC_DRAW
        );
    }

    bool allocateFrameResources()
    {
        updateRenderResolution();
        if (
            allocated_width == render_width &&
            allocated_height == render_height &&
            surface_current != 0u)
        {
            return true;
        }

        const std::size_t pixels = pixelCount();
        const std::size_t queue_bytes =
            PathTracerGpu::HIT_QUEUE_OFFSET + pixels * sizeof(std::uint32_t);
        const bool ok =
            ensureBuffer(surface_current, pixels * sizeof(PathTracerGpu::Surface), GL_DYNAMIC_DRAW) &&
            ensureBuffer(surface_previous, pixels * sizeof(PathTracerGpu::Surface), GL_DYNAMIC_DRAW) &&
            ensureBuffer(secondary_surface, bounce_capacity * sizeof(PathTracerGpu::Surface), GL_DYNAMIC_DRAW) &&
            ensureBuffer(bounce_ray_buffer, bounce_capacity * sizeof(PathTracerGpu::Ray), GL_DYNAMIC_DRAW) &&
            ensureBuffer(sorted_bounce_ray_buffer, bounce_capacity * sizeof(PathTracerGpu::Ray), GL_DYNAMIC_DRAW) &&
            ensureBuffer(queue_control_buffer, queue_bytes, GL_DYNAMIC_DRAW) &&
            ensureBuffer(reservoir_work_a, pixels * sizeof(PathTracerGpu::Reservoir), GL_DYNAMIC_DRAW) &&
            ensureBuffer(reservoir_work_b, pixels * sizeof(PathTracerGpu::Reservoir), GL_DYNAMIC_DRAW) &&
            ensureBuffer(reservoir_previous, pixels * sizeof(PathTracerGpu::Reservoir), GL_DYNAMIC_DRAW) &&
            ensureBuffer(lighting_current, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(lighting_temporal, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(lighting_history, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(moments_current, pixels * sizeof(PathTracerGpu::Moments), GL_DYNAMIC_DRAW) &&
            ensureBuffer(moments_previous, pixels * sizeof(PathTracerGpu::Moments), GL_DYNAMIC_DRAW) &&
            allocatePersistentResources() &&
            createOutputTexture();

        if (!ok) return false;
        allocated_width = render_width;
        allocated_height = render_height;
        history_valid = false;
        std::fprintf(
            stderr,
            "[PathTracer]: frame resources %dx%d, GI queue capacity %zu\n",
            render_width,
            render_height,
            bounce_capacity
        );
        return true;
    }

    void destroyFrameResources()
    {
        deleteBuffer(surface_current);
        deleteBuffer(surface_previous);
        deleteBuffer(secondary_surface);
        deleteBuffer(bounce_ray_buffer);
        deleteBuffer(sorted_bounce_ray_buffer);
        deleteBuffer(queue_control_buffer);
        deleteBuffer(reservoir_work_a);
        deleteBuffer(reservoir_work_b);
        deleteBuffer(reservoir_previous);
        deleteBuffer(lighting_current);
        deleteBuffer(lighting_temporal);
        deleteBuffer(lighting_history);
        deleteBuffer(moments_current);
        deleteBuffer(moments_previous);
        if (output_texture != 0u) glDeleteTextures(1, &output_texture);
        output_texture = 0u;
        allocated_width = 0;
        allocated_height = 0;
        history_valid = false;
    }

    bool createPrograms()
    {
        primary_trace_program = createComputeProgram({
            WavefrontShaders::common,
            WavefrontRuntimeShaders::frame_index_compat,
            PrimaryTraceShader::source,
        });
        prepare_dispatch_program = createComputeProgram({WavefrontShaders::prepare_dispatch});
        primary_shade_program = createComputeProgram({
            WavefrontShaders::common,
            WavefrontRuntimeShaders::frame_index_compat,
            WavefrontShaders::primary_shade,
        });
        classify_bounces_program = createComputeProgram({WavefrontShaders::classify_bounces});
        prepare_reorder_program = createComputeProgram({WavefrontShaders::prepare_reorder});
        scatter_bounces_program = createComputeProgram({WavefrontShaders::scatter_bounces});
        bounce_intersect_program = createComputeProgram({
            WavefrontShaders::common,
            WavefrontRuntimeShaders::frame_index_compat,
            WavefrontShaders::bounce_intersect,
        });
        bounce_shade_program = createComputeProgram({
            WavefrontShaders::common,
            WavefrontRuntimeShaders::frame_index_compat,
            WavefrontRuntimeShaders::bounce_shade,
        });
        restir_temporal_program = createComputeProgram({RestirShaders::common, RestirShaders::temporal_reuse});
        restir_spatial_program = createComputeProgram({RestirShaders::common, RestirShaders::spatial_reuse});
        compose_program = createComputeProgram({SvgfShaders::common, SvgfShaders::compose});
        temporal_filter_program = createComputeProgram({SvgfShaders::common, SvgfShaders::temporal_filter});
        atrous_program = createComputeProgram({SvgfShaders::common, SvgfShaders::atrous});
        copy_output_program = createComputeProgram({SvgfShaders::copy_to_image});
        clear_cache_program = createComputeProgram({RadianceCacheShaders::clear_cache});
        present_program = createGraphicsProgram(SvgfShaders::present_vertex, SvgfShaders::present_fragment);

        return
            primary_trace_program != 0u &&
            prepare_dispatch_program != 0u &&
            primary_shade_program != 0u &&
            classify_bounces_program != 0u &&
            prepare_reorder_program != 0u &&
            scatter_bounces_program != 0u &&
            bounce_intersect_program != 0u &&
            bounce_shade_program != 0u &&
            restir_temporal_program != 0u &&
            restir_spatial_program != 0u &&
            compose_program != 0u &&
            temporal_filter_program != 0u &&
            atrous_program != 0u &&
            copy_output_program != 0u &&
            clear_cache_program != 0u &&
            present_program != 0u;
    }

    void destroyPrograms()
    {
        GLuint *programs[] = {
            &primary_trace_program,
            &prepare_dispatch_program,
            &primary_shade_program,
            &classify_bounces_program,
            &prepare_reorder_program,
            &scatter_bounces_program,
            &bounce_intersect_program,
            &bounce_shade_program,
            &restir_temporal_program,
            &restir_spatial_program,
            &compose_program,
            &temporal_filter_program,
            &atrous_program,
            &copy_output_program,
            &clear_cache_program,
            &present_program,
        };
        for (GLuint *program : programs) {
            if (*program != 0u && GL20.glDeleteProgram) GL20.glDeleteProgram(*program);
            *program = 0u;
        }
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

    void syncTextureSlots()
    {
        texture_slots.fill(0u);
        const auto& handles = scene.textureHandles();
        for (std::size_t slot = 0u; slot < handles.size(); ++slot) {
            if (handles[slot] != Models::INVALID_TEXTURE) {
                texture_slots[slot] = textureFor(handles[slot]);
            }
        }
    }

    CameraState cameraState(const Ecs::World& world) const
    {
        CameraState state;
        const Ecs::Entity camera_entity = Camera::activeCamera(world);
        if (camera_entity == Ecs::INVALID_ENTITY) return state;
        const Transform *transform = world.get<Transform>(camera_entity);
        const Camera::CameraComponent *camera = world.get<Camera::CameraComponent>(camera_entity);
        if (!transform || !camera) return state;

        const Vec3 forward_value = Camera::flightDirection(transform->rotation.y, transform->rotation.x);
        const Vec3 right_value = Camera::strafeDirection(transform->rotation.y);
        state.valid = true;
        state.position = {transform->position.x, transform->position.y, transform->position.z};
        state.forward = normalize({forward_value.x, forward_value.y, forward_value.z});
        state.right = normalize({right_value.x, right_value.y, right_value.z});
        state.up = normalize(cross(state.right, state.forward));
        state.fov_degrees = std::clamp(camera->fov_degrees, 1.0f, 179.0f);
        const float aspect = static_cast<float>(render_width) / static_cast<float>(std::max(render_height, 1));
        state.view_projection = multiply(
            perspectiveMatrix(state.fov_degrees, aspect, 0.05f, 10000.0f),
            viewMatrix(state.position, state.forward, state.right, state.up)
        );
        return state;
    }

    LightState lightState(const Ecs::World& world) const
    {
        LightState result;
        world.each<LightComponent, Transform>(
            [&](Ecs::Entity, const LightComponent& light, const Transform& transform) {
                if (result.valid || light.type != LightType::Point) return;
                result.valid = true;
                result.position = {transform.position.x, transform.position.y, transform.position.z};
                result.color = {light.color.x, light.color.y, light.color.z};
                result.intensity = std::max(light.intensity, 0.0f);
            }
        );
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

    bool syncSceneGpu(const PathTracerScene::SyncResult& sync)
    {
        if (!sync.ok) {
            std::fprintf(stderr, "[PathTracer]: scene sync failed: %s\n", sync.error.c_str());
            return false;
        }

        const auto& nodes = scene.nodes();
        const auto& triangles = scene.triangles();
        const auto& instances = scene.instances();
        const auto& materials = scene.materials();

        if (sync.full_upload) {
            const bool ok =
                uploadBuffer(
                    node_buffer,
                    nodes.data(),
                    nodes.size() * sizeof(PathTracerAccel::WideNode),
                    GL_DYNAMIC_DRAW
                ) &&
                uploadBuffer(
                    triangle_buffer,
                    triangles.data(),
                    triangles.size() * sizeof(PathTracerAccel::Triangle),
                    GL_DYNAMIC_DRAW
                ) &&
                uploadBuffer(
                    instance_buffer,
                    instances.data(),
                    instances.size() * sizeof(PathTracerGpu::Instance),
                    GL_DYNAMIC_DRAW
                ) &&
                uploadBuffer(
                    material_buffer,
                    materials.data(),
                    materials.size() * sizeof(PathTracerGpu::Material),
                    GL_STATIC_DRAW
                );
            if (!ok) return false;
            syncTextureSlots();
            std::fprintf(
                stderr,
                "[PathTracer]: scene cache %zu TLAS nodes, %zu total nodes, %zu triangles, %zu instances\n",
                scene.tlasNodeCount(),
                nodes.size(),
                triangles.size(),
                instances.size()
            );
        } else {
            for (const PathTracerScene::DirtyRange& range : sync.node_ranges) {
                if (range.first + range.count > nodes.size()) return false;
                updateBufferRange(
                    node_buffer,
                    sizeof(PathTracerAccel::WideNode),
                    range.first,
                    range.count,
                    nodes.data()
                );
            }
            for (const PathTracerScene::DirtyRange& range : sync.triangle_ranges) {
                if (range.first + range.count > triangles.size()) return false;
                updateBufferRange(
                    triangle_buffer,
                    sizeof(PathTracerAccel::Triangle),
                    range.first,
                    range.count,
                    triangles.data()
                );
            }
            if (sync.instances_dirty) {
                GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, instance_buffer);
                GL15.glBufferSubData(
                    GL_SHADER_STORAGE_BUFFER,
                    0,
                    static_cast<LWCGLsizeiptr>(instances.size() * sizeof(PathTracerGpu::Instance)),
                    instances.data()
                );
                GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
            }
        }

        if (sync.invalidate_history) history_valid = false;
        if (sync.clear_radiance_cache) cache_needs_clear = true;
        return true;
    }

    void bindAccel(GLuint program)
    {
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, instance_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, material_buffer);
        setInt(program, "uTlasNodeCount", static_cast<int>(scene.tlasNodeCount()));
        setInt(program, "uNodeCount", static_cast<int>(scene.nodes().size()));
        setInt(program, "uInstanceCount", static_cast<int>(scene.instances().size()));
        setInt(program, "uMaterialCount", static_cast<int>(scene.materials().size()));
    }

    void bindTextures(GLuint program)
    {
        for (std::size_t slot = 0u; slot < texture_slots.size(); ++slot) {
            GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
            glBindTexture(GL_TEXTURE_2D, texture_slots[slot]);
            char name[32]{};
            std::snprintf(name, sizeof(name), "uTexture%zu", slot);
            setInt(program, name, static_cast<int>(slot));
        }
        GLModern.glActiveTexture(GL_TEXTURE0);
    }

    void setLightUniforms(GLuint program, const LightState& light)
    {
        setInt(program, "uHasLight", light.valid ? 1 : 0);
        setVec3(program, "uLightPosition", light.position);
        setVec3(program, "uLightColor", light.color);
        setFloat(program, "uLightIntensity", light.intensity);
    }

    void setFrameIndex(GLuint program)
    {
        setInt(program, "uFrameIndexInt", static_cast<int>(frame_index & 0x7fffffffu));
    }

    void resetQueueControl()
    {
        PathTracerGpu::QueueControl control{};
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, queue_control_buffer);
        GL15.glBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            static_cast<LWCGLsizeiptr>(sizeof(control)),
            &control
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
    }

    void clearRadianceCache()
    {
        if (!cache_needs_clear) return;
        GL20.glUseProgram(clear_cache_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, radiance_cache);
        setInt(
            clear_cache_program,
            "uCacheSize",
            static_cast<int>(PathTracerGpu::RADIANCE_CACHE_ENTRIES)
        );
        GL43.glDispatchCompute(
            static_cast<GLuint>((PathTracerGpu::RADIANCE_CACHE_ENTRIES + 63u) / 64u),
            1u,
            1u
        );
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        GL20.glUseProgram(0u);
        cache_needs_clear = false;
    }

    void prepareDispatchCommands()
    {
        GL20.glUseProgram(prepare_dispatch_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, queue_control_buffer);
        GL43.glDispatchCompute(1u, 1u, 1u);
        GL42.glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        GL20.glUseProgram(0u);
    }

    void dispatch2D()
    {
        GL43.glDispatchCompute(
            static_cast<GLuint>((render_width + 7) / 8),
            static_cast<GLuint>((render_height + 7) / 8),
            1u
        );
    }

    void dispatchIndirect(std::size_t offset)
    {
        GL15.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, queue_control_buffer);
        GL43.glDispatchComputeIndirect(static_cast<LWCGLintptr>(offset));
        GL15.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0u);
    }

    void dispatchFrame(const CameraState& camera, const LightState& light)
    {
        const int pixels = static_cast<int>(pixelCount());
        resetQueueControl();
        clearRadianceCache();

        GL20.glUseProgram(primary_trace_program);
        bindAccel(primary_trace_program);
        setFrameIndex(primary_trace_program);
        setVec3(primary_trace_program, "uCameraPosition", camera.position);
        setVec3(primary_trace_program, "uCameraForward", camera.forward);
        setVec3(primary_trace_program, "uCameraRight", camera.right);
        setVec3(primary_trace_program, "uCameraUp", camera.up);
        setFloat(
            primary_trace_program,
            "uTanHalfFov",
            std::tan(camera.fov_degrees * (kPi / 360.0f))
        );
        setFloat(
            primary_trace_program,
            "uAspect",
            static_cast<float>(render_width) / static_cast<float>(std::max(render_height, 1))
        );
        setInt(primary_trace_program, "uResolutionX", render_width);
        setInt(primary_trace_program, "uResolutionY", render_height);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, queue_control_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, reservoir_work_a);
        dispatch2D();
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        prepareDispatchCommands();

        GL20.glUseProgram(primary_shade_program);
        bindAccel(primary_shade_program);
        bindTextures(primary_shade_program);
        setLightUniforms(primary_shade_program, light);
        setFrameIndex(primary_shade_program);
        setInt(primary_shade_program, "uPixelCount", pixels);
        setInt(primary_shade_program, "uResolutionX", render_width);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, queue_control_buffer);
        dispatchIndirect(PathTracerGpu::HIT_DISPATCH_OFFSET);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        prepareDispatchCommands();

        GL20.glUseProgram(classify_bounces_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, queue_control_buffer);
        setInt(classify_bounces_program, "uPixelCount", pixels);
        dispatchIndirect(PathTracerGpu::BOUNCE_DISPATCH_OFFSET);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(prepare_reorder_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, queue_control_buffer);
        GL43.glDispatchCompute(1u, 1u, 1u);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(scatter_bounces_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, sorted_bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, queue_control_buffer);
        setInt(scatter_bounces_program, "uPixelCount", pixels);
        dispatchIndirect(PathTracerGpu::BOUNCE_DISPATCH_OFFSET);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(bounce_intersect_program);
        bindAccel(bounce_intersect_program);
        setInt(bounce_intersect_program, "uPixelCount", pixels);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, sorted_bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, secondary_surface);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, queue_control_buffer);
        dispatchIndirect(PathTracerGpu::BOUNCE_DISPATCH_OFFSET);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(bounce_shade_program);
        bindAccel(bounce_shade_program);
        bindTextures(bounce_shade_program);
        setLightUniforms(bounce_shade_program, light);
        setFrameIndex(bounce_shade_program);
        setInt(bounce_shade_program, "uPixelCount", pixels);
        setInt(
            bounce_shade_program,
            "uCacheSizeInt",
            static_cast<int>(PathTracerGpu::RADIANCE_CACHE_ENTRIES)
        );
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, secondary_surface);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, reservoir_work_a);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7u, radiance_cache);
        dispatchIndirect(PathTracerGpu::BOUNCE_DISPATCH_OFFSET);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(restir_temporal_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, surface_previous);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, reservoir_work_a);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, reservoir_previous);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, reservoir_work_b);
        setInt(restir_temporal_program, "uResolutionX", render_width);
        setInt(restir_temporal_program, "uResolutionY", render_height);
        setInt(restir_temporal_program, "uFrameIndex", static_cast<int>(frame_index & 0x7fffffffu));
        setInt(restir_temporal_program, "uHistoryValid", history_valid ? 1 : 0);
        setMat4(restir_temporal_program, "uPreviousViewProjection", previous_view_projection);
        setVec3(restir_temporal_program, "uPreviousCameraPosition", previous_camera_position);
        dispatch2D();
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(restir_spatial_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, reservoir_work_b);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, reservoir_work_a);
        setInt(restir_spatial_program, "uResolutionX", render_width);
        setInt(restir_spatial_program, "uResolutionY", render_height);
        setInt(restir_spatial_program, "uFrameIndex", static_cast<int>(frame_index & 0x7fffffffu));
        dispatch2D();
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(compose_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, reservoir_work_a);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, lighting_current);
        setInt(compose_program, "uResolutionX", render_width);
        setInt(compose_program, "uResolutionY", render_height);
        dispatch2D();
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(temporal_filter_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, surface_previous);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, lighting_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, lighting_history);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, moments_previous);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, lighting_temporal);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, moments_current);
        setInt(temporal_filter_program, "uResolutionX", render_width);
        setInt(temporal_filter_program, "uResolutionY", render_height);
        setInt(temporal_filter_program, "uHistoryValid", history_valid ? 1 : 0);
        setMat4(temporal_filter_program, "uPreviousViewProjection", previous_view_projection);
        setVec3(temporal_filter_program, "uPreviousCameraPosition", previous_camera_position);
        dispatch2D();
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GLuint atrous_input = lighting_temporal;
        GLuint atrous_output = lighting_current;
        for (int iteration = 0; iteration < 3; ++iteration) {
            if (iteration == 1) {
                atrous_input = lighting_current;
                atrous_output = lighting_history;
            } else if (iteration == 2) {
                atrous_input = lighting_history;
                atrous_output = lighting_current;
            }

            GL20.glUseProgram(atrous_program);
            GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
            GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, atrous_input);
            GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, moments_current);
            GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, atrous_output);
            setInt(atrous_program, "uResolutionX", render_width);
            setInt(atrous_program, "uResolutionY", render_height);
            setInt(atrous_program, "uStep", 1 << iteration);
            dispatch2D();
            GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        GL20.glUseProgram(copy_output_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, lighting_current);
        GL42.glBindImageTexture(0u, output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        setInt(copy_output_program, "uResolutionX", render_width);
        setInt(copy_output_program, "uResolutionY", render_height);
        dispatch2D();
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        GL20.glUseProgram(0u);

        std::swap(surface_current, surface_previous);
        std::swap(reservoir_work_a, reservoir_previous);
        std::swap(lighting_temporal, lighting_history);
        std::swap(moments_current, moments_previous);
        previous_view_projection = camera.view_projection;
        previous_camera_position = camera.position;
        history_valid = true;
        ++frame_index;
    }

    void present()
    {
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        GL20.glUseProgram(present_program);
        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, output_texture);
        setInt(present_program, "uOutput", 0);
        setFloat(present_program, "uExposure", settings.exposure);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(3.0f, -1.0f);
        glVertex2f(-1.0f, 3.0f);
        glEnd();
        GL20.glUseProgram(0u);
    }
};

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
        !GL43.glDispatchComputeIndirect ||
        !GL42.glBindImageTexture ||
        !GL42.glMemoryBarrier ||
        !GL30.glBindBufferBase)
    {
        std::fprintf(
            stderr,
            "[PathTracer]: OpenGL 4.3 compute/indirect dispatch required; found %d.%d\n",
            major,
            minor
        );
        return false;
    }

    if (!impl_->createPrograms() || !impl_->allocateFrameResources()) {
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
        "[PathTracer]: wavefront OpenGL %d.%d, native primary %dx%d, 1/4 GI + ReSTIR/SVGF\n",
        major,
        minor,
        impl_->render_width,
        impl_->render_height
    );
    return true;
}

void PathTracer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    impl_->updateRenderResolution();
    if (!impl_->initialized) return;
    if (!impl_->allocateFrameResources()) {
        std::fprintf(stderr, "[PathTracer]: failed to resize frame resources\n");
        shutdown();
    }
}

void PathTracer::render(const Ecs::World& world)
{
    if (!impl_->active()) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    if (!impl_->allocateFrameResources()) {
        std::fprintf(stderr, "[PathTracer]: frame resource allocation failed\n");
        return;
    }

    const Impl::CameraState camera = impl_->cameraState(world);
    if (!camera.valid) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const PathTracerScene::SyncResult scene_sync = impl_->scene.sync(world);
    if (!impl_->syncSceneGpu(scene_sync)) return;

    const Impl::LightState light = impl_->lightState(world);
    const std::uint64_t next_light_signature = impl_->lightSignature(light);
    if (next_light_signature != impl_->light_signature) {
        impl_->light_signature = next_light_signature;
        impl_->cache_needs_clear = true;
        impl_->history_valid = false;
    }

    impl_->dispatchFrame(camera, light);
    impl_->present();
}

void PathTracer::shutdown()
{
    if (!impl_) return;
    if (GL20.glUseProgram) GL20.glUseProgram(0u);
    impl_->destroyPrograms();
    impl_->destroyFrameResources();
    impl_->deleteBuffer(impl_->radiance_cache);
    impl_->deleteBuffer(impl_->node_buffer);
    impl_->deleteBuffer(impl_->triangle_buffer);
    impl_->deleteBuffer(impl_->instance_buffer);
    impl_->deleteBuffer(impl_->material_buffer);

    for (const auto& [handle, texture] : impl_->texture_cache) {
        (void)handle;
        if (texture != 0u) glDeleteTextures(1, &texture);
    }
    impl_->texture_cache.clear();
    impl_->texture_slots.fill(0u);
    impl_->scene.clear();
    impl_->frame_index = 0u;
    impl_->light_signature = 0u;
    impl_->history_valid = false;
    impl_->cache_needs_clear = true;
    impl_->initialized = false;
}

bool PathTracer::initialized() const { return impl_->initialized; }
bool PathTracer::enabled() const { return impl_->settings.enabled; }

void PathTracer::setEnabled(bool enabled)
{
    impl_->settings.enabled = enabled;
    impl_->history_valid = false;
}

PathTracerSettings& PathTracer::settings() { return impl_->settings; }
const PathTracerSettings& PathTracer::settings() const { return impl_->settings; }

} // namespace Renderer
