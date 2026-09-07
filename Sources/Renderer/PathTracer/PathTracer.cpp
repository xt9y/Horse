#include "Renderer/PathTracer/PathTracer.hpp"

#include "Animation/Animation.hpp"
#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/PathTracerGpu.hpp"
#include "Renderer/PathTracer/RadianceCacheShaders.hpp"
#include "Renderer/PathTracer/RestirShaders.hpp"
#include "Renderer/PathTracer/SvgfShaders.hpp"
#include "Renderer/PathTracer/WavefrontShaders.hpp"
#include "Renderer/PathTracer/WideBvh.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
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

using Mat4 = std::array<float, 16>;
using AccelTriangle = PathTracerAccel::Triangle;
using WideNode = PathTracerAccel::WideNode;

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct alignas(16) GpuMaterial {
    std::array<float, 4> base_color {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<std::int32_t, 4> data {-1, 0, 0, 0};
};

static_assert(sizeof(GpuMaterial) == 32u);

constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kMaximumTriangles = 1000000u;
constexpr std::size_t kMaximumTextureSlots = 16u;

Vec3f add(const Vec3f& a, const Vec3f& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f subtract(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

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

Mat4 translation(float x, float y, float z)
{
    Mat4 result = identityMatrix();
    result[12] = x;
    result[13] = y;
    result[14] = z;
    return result;
}

Mat4 scaling(float x, float y, float z)
{
    Mat4 result{};
    result[0] = x;
    result[5] = y;
    result[10] = z;
    result[15] = 1.0f;
    return result;
}

Mat4 rotationX(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[5] = c;
    result[6] = s;
    result[9] = -s;
    result[10] = c;
    return result;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[2] = -s;
    result[8] = s;
    result[10] = c;
    return result;
}

Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[1] = s;
    result[4] = -s;
    result[5] = c;
    return result;
}

Mat4 modelMatrix(const Transform& transform)
{
    return multiply(
        multiply(
            multiply(
                multiply(
                    translation(transform.position.x, transform.position.y, transform.position.z),
                    rotationX(transform.rotation.x)
                ),
                rotationY(transform.rotation.y)
            ),
            rotationZ(transform.rotation.z)
        ),
        scaling(transform.scale.x, transform.scale.y, transform.scale.z)
    );
}

Mat4 inverseModelMatrix(const Transform& transform)
{
    const float sx = std::abs(transform.scale.x) > 1.0e-8f ? 1.0f / transform.scale.x : 0.0f;
    const float sy = std::abs(transform.scale.y) > 1.0e-8f ? 1.0f / transform.scale.y : 0.0f;
    const float sz = std::abs(transform.scale.z) > 1.0e-8f ? 1.0f / transform.scale.z : 0.0f;
    return multiply(
        multiply(
            multiply(
                multiply(scaling(sx, sy, sz), rotationZ(-transform.rotation.z)),
                rotationY(-transform.rotation.y)
            ),
            rotationX(-transform.rotation.x)
        ),
        translation(-transform.position.x, -transform.position.y, -transform.position.z)
    );
}

Vec3f transformPoint(const Mat4& matrix, const Vec3f& point)
{
    return {
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
    };
}

Vec3f transformNormal(const Mat4& world_to_object, const Vec3f& normal)
{
    return normalize({
        world_to_object[0] * normal.x + world_to_object[1] * normal.y + world_to_object[2] * normal.z,
        world_to_object[4] * normal.x + world_to_object[5] * normal.y + world_to_object[6] * normal.z,
        world_to_object[8] * normal.x + world_to_object[9] * normal.y + world_to_object[10] * normal.z,
    });
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

void setUInt(GLuint program, const char *name, std::uint32_t value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    // glUniform1ui is not exposed by lwcgl v2.9.3; values used here stay below INT_MAX.
    if (location >= 0) GL20.glUniform1i(location, static_cast<GLint>(value));
}

void setFloat(GLuint program, const char *name, float value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setVec2i(GLuint program, const char *name, int x, int y)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) {
        // lwcgl currently exposes glUniform2f but not glUniform2i. GLSL accepts exact integer values
        // only through an integer setter, so resolution shaders use a float-backed helper uniform path
        // only where the declaration is vec2. ivec2 declarations are handled by setResolutionUniforms().
        (void)x;
        (void)y;
    }
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
    bool initialized = false;
    bool history_valid = false;
    bool cache_needs_clear = true;
    int width = 1;
    int height = 1;
    int render_width = 1;
    int render_height = 1;
    int allocated_width = 0;
    int allocated_height = 0;
    std::uint32_t frame_index = 0u;
    std::uint64_t scene_signature = 0u;
    std::uint64_t light_signature = 0u;
    Mat4 previous_view_projection = identityMatrix();
    Vec3f previous_camera_position{};

    GLuint primary_generate_program = 0u;
    GLuint primary_intersect_program = 0u;
    GLuint prepare_dispatch_program = 0u;
    GLuint primary_shade_program = 0u;
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
    GLuint material_buffer = 0u;
    GLuint primary_ray_buffer = 0u;
    GLuint surface_current = 0u;
    GLuint surface_previous = 0u;
    GLuint secondary_surface = 0u;
    GLuint hit_queue_buffer = 0u;
    GLuint bounce_ray_buffer = 0u;
    GLuint counter_buffer = 0u;
    GLuint dispatch_buffer = 0u;
    GLuint reservoir_work_a = 0u;
    GLuint reservoir_work_b = 0u;
    GLuint reservoir_previous = 0u;
    GLuint direct_lighting = 0u;
    GLuint lighting_current = 0u;
    GLuint lighting_temporal = 0u;
    GLuint lighting_history = 0u;
    GLuint moments_current = 0u;
    GLuint moments_previous = 0u;
    GLuint radiance_cache = 0u;
    GLuint output_texture = 0u;

    std::size_t node_count = 0u;
    std::size_t triangle_count = 0u;
    std::size_t material_count = 0u;
    std::unordered_map<std::uint32_t, GLuint> texture_cache;
    std::array<GLuint, kMaximumTextureSlots> texture_slots{};
    std::size_t texture_slot_count = 0u;

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

    bool allocateFrameResources()
    {
        updateRenderResolution();
        if (allocated_width == render_width && allocated_height == render_height) return true;

        const std::size_t pixels = pixelCount();
        const bool ok =
            ensureBuffer(primary_ray_buffer, pixels * sizeof(PathTracerGpu::Ray), GL_DYNAMIC_DRAW) &&
            ensureBuffer(surface_current, pixels * sizeof(PathTracerGpu::Surface), GL_DYNAMIC_DRAW) &&
            ensureBuffer(surface_previous, pixels * sizeof(PathTracerGpu::Surface), GL_DYNAMIC_DRAW) &&
            ensureBuffer(secondary_surface, pixels * sizeof(PathTracerGpu::Surface), GL_DYNAMIC_DRAW) &&
            ensureBuffer(hit_queue_buffer, pixels * sizeof(std::uint32_t), GL_DYNAMIC_DRAW) &&
            ensureBuffer(bounce_ray_buffer, pixels * sizeof(PathTracerGpu::Ray), GL_DYNAMIC_DRAW) &&
            ensureBuffer(counter_buffer, sizeof(PathTracerGpu::QueueCounters), GL_DYNAMIC_DRAW) &&
            ensureBuffer(dispatch_buffer, sizeof(PathTracerGpu::DispatchCommands), GL_DYNAMIC_DRAW) &&
            ensureBuffer(reservoir_work_a, pixels * sizeof(PathTracerGpu::Reservoir), GL_DYNAMIC_DRAW) &&
            ensureBuffer(reservoir_work_b, pixels * sizeof(PathTracerGpu::Reservoir), GL_DYNAMIC_DRAW) &&
            ensureBuffer(reservoir_previous, pixels * sizeof(PathTracerGpu::Reservoir), GL_DYNAMIC_DRAW) &&
            ensureBuffer(direct_lighting, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(lighting_current, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(lighting_temporal, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(lighting_history, pixels * sizeof(PathTracerGpu::Lighting), GL_DYNAMIC_DRAW) &&
            ensureBuffer(moments_current, pixels * sizeof(PathTracerGpu::Moments), GL_DYNAMIC_DRAW) &&
            ensureBuffer(moments_previous, pixels * sizeof(PathTracerGpu::Moments), GL_DYNAMIC_DRAW) &&
            ensureBuffer(
                radiance_cache,
                PathTracerGpu::RADIANCE_CACHE_ENTRIES * sizeof(PathTracerGpu::RadianceCacheEntry),
                GL_DYNAMIC_DRAW
            ) &&
            createOutputTexture();

        if (!ok) return false;
        allocated_width = render_width;
        allocated_height = render_height;
        history_valid = false;
        cache_needs_clear = true;
        std::fprintf(
            stderr,
            "[PathTracer]: frame resources %dx%d (%zu pixels)\n",
            render_width,
            render_height,
            pixels
        );
        return true;
    }

    void destroyFrameResources()
    {
        deleteBuffer(primary_ray_buffer);
        deleteBuffer(surface_current);
        deleteBuffer(surface_previous);
        deleteBuffer(secondary_surface);
        deleteBuffer(hit_queue_buffer);
        deleteBuffer(bounce_ray_buffer);
        deleteBuffer(counter_buffer);
        deleteBuffer(dispatch_buffer);
        deleteBuffer(reservoir_work_a);
        deleteBuffer(reservoir_work_b);
        deleteBuffer(reservoir_previous);
        deleteBuffer(direct_lighting);
        deleteBuffer(lighting_current);
        deleteBuffer(lighting_temporal);
        deleteBuffer(lighting_history);
        deleteBuffer(moments_current);
        deleteBuffer(moments_previous);
        deleteBuffer(radiance_cache);
        if (output_texture != 0u) glDeleteTextures(1, &output_texture);
        output_texture = 0u;
        allocated_width = 0;
        allocated_height = 0;
        history_valid = false;
    }

    bool createPrograms()
    {
        primary_generate_program = createComputeProgram({WavefrontShaders::common, WavefrontShaders::primary_generate});
        primary_intersect_program = createComputeProgram({WavefrontShaders::common, WavefrontShaders::primary_intersect});
        prepare_dispatch_program = createComputeProgram({WavefrontShaders::common, WavefrontShaders::prepare_dispatch});
        primary_shade_program = createComputeProgram({WavefrontShaders::common, WavefrontShaders::primary_shade});
        bounce_intersect_program = createComputeProgram({WavefrontShaders::common, WavefrontShaders::bounce_intersect});
        bounce_shade_program = createComputeProgram({WavefrontShaders::common, WavefrontShaders::bounce_shade});
        restir_temporal_program = createComputeProgram({RestirShaders::common, RestirShaders::temporal_reuse});
        restir_spatial_program = createComputeProgram({RestirShaders::common, RestirShaders::spatial_reuse});
        compose_program = createComputeProgram({SvgfShaders::common, SvgfShaders::compose});
        temporal_filter_program = createComputeProgram({SvgfShaders::common, SvgfShaders::temporal_filter});
        atrous_program = createComputeProgram({SvgfShaders::common, SvgfShaders::atrous});
        copy_output_program = createComputeProgram({SvgfShaders::copy_to_image});
        clear_cache_program = createComputeProgram({RadianceCacheShaders::clear_cache});
        present_program = createGraphicsProgram(SvgfShaders::present_vertex, SvgfShaders::present_fragment);
        return
            primary_generate_program != 0u && primary_intersect_program != 0u &&
            prepare_dispatch_program != 0u && primary_shade_program != 0u &&
            bounce_intersect_program != 0u && bounce_shade_program != 0u &&
            restir_temporal_program != 0u && restir_spatial_program != 0u &&
            compose_program != 0u && temporal_filter_program != 0u &&
            atrous_program != 0u && copy_output_program != 0u &&
            clear_cache_program != 0u && present_program != 0u;
    }

    void destroyPrograms()
    {
        GLuint *programs[] = {
            &primary_generate_program,
            &primary_intersect_program,
            &prepare_dispatch_program,
            &primary_shade_program,
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
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) return 0u;

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

    std::uint64_t sceneSignature(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;
            hashValue(hash, entity);
            hashValue(hash, mesh_component->mesh);
            hashValue(hash, mesh_component->material);
            hashTransform(hash, *transform);
            const Animation::SkinBindingComponent *binding = world.get<Animation::SkinBindingComponent>(entity);
            if (binding && binding->animator != Ecs::INVALID_ENTITY) {
                const Animation::AnimatorComponent *animator = world.get<Animation::AnimatorComponent>(binding->animator);
                if (animator) hashValue(hash, animator->pose.revision);
            }
        }
        return hash;
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

    bool syncScene(const Ecs::World& world)
    {
        const auto start_time = std::chrono::steady_clock::now();
        texture_slots.fill(0u);
        texture_slot_count = 0u;

        std::vector<AccelTriangle> triangles;
        triangles.reserve(200000u);
        std::vector<GpuMaterial> materials;
        std::unordered_map<std::uint32_t, std::uint32_t> material_indices;
        std::unordered_map<std::uint32_t, int> texture_indices;

        auto materialIndex = [&](std::uint32_t handle) -> std::uint32_t {
            const auto found = material_indices.find(handle);
            if (found != material_indices.end()) return found->second;
            GpuMaterial gpu;
            const Models::MaterialData *material = Models::material(handle);
            if (material) {
                gpu.base_color = {
                    material->color.x,
                    material->color.y,
                    material->color.z,
                    std::clamp(material->opacity, 0.0f, 1.0f),
                };
                if (material->diffuse_texture != Models::INVALID_TEXTURE) {
                    int slot = -1;
                    const auto tf = texture_indices.find(material->diffuse_texture);
                    if (tf != texture_indices.end()) {
                        slot = tf->second;
                    } else if (texture_slot_count < texture_slots.size()) {
                        const GLuint texture = textureFor(material->diffuse_texture);
                        if (texture != 0u) {
                            slot = static_cast<int>(texture_slot_count);
                            texture_slots[texture_slot_count++] = texture;
                            texture_indices.emplace(material->diffuse_texture, slot);
                        }
                    }
                    gpu.data[0] = slot;
                }
            }
            const std::uint32_t index = static_cast<std::uint32_t>(materials.size());
            materials.push_back(gpu);
            material_indices.emplace(handle, index);
            return index;
        };

        for (const Ecs::Entity entity : world.entities()) {
            if (triangles.size() >= kMaximumTriangles) break;
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;
            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (!mesh || mesh->indices.size() < 3u) continue;
            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.5f) continue;

            const Animation::Pose *pose = nullptr;
            const Animation::SkinBindingComponent *binding = world.get<Animation::SkinBindingComponent>(entity);
            if (binding && binding->animator != Ecs::INVALID_ENTITY) {
                const Animation::AnimatorComponent *animator = world.get<Animation::AnimatorComponent>(binding->animator);
                if (animator && !animator->pose.skin.empty()) pose = &animator->pose;
            }

            std::vector<Models::Vec3> skinned_positions;
            std::vector<Models::Vec3> skinned_normals;
            if (pose) {
                skinned_positions.resize(mesh->vertices.size());
                skinned_normals.resize(mesh->vertices.size());
                for (std::size_t vertex_index = 0u; vertex_index < mesh->vertices.size(); ++vertex_index) {
                    const Models::Vertex& vertex = mesh->vertices[vertex_index];
                    Animation::Vec3 out_position{};
                    Animation::Vec3 out_normal{};
                    Animation::skinVertex(
                        *pose,
                        vertex.skin,
                        {vertex.position.x, vertex.position.y, vertex.position.z},
                        {vertex.normal.x, vertex.normal.y, vertex.normal.z},
                        &out_position,
                        &out_normal
                    );
                    skinned_positions[vertex_index] = {out_position.x, out_position.y, out_position.z};
                    skinned_normals[vertex_index] = {out_normal.x, out_normal.y, out_normal.z};
                }
            }

            const Mat4 model = modelMatrix(*transform);
            const Mat4 inverse_model = inverseModelMatrix(*transform);
            const std::uint32_t gpu_material = materialIndex(mesh_component->material);
            const std::size_t mesh_triangle_count = mesh->indices.size() / 3u;

            for (std::size_t triangle_index = 0u; triangle_index < mesh_triangle_count; ++triangle_index) {
                if (triangles.size() >= kMaximumTriangles) break;
                const std::size_t offset = triangle_index * 3u;
                const std::uint32_t i0 = mesh->indices[offset + 0u];
                const std::uint32_t i1 = mesh->indices[offset + 1u];
                const std::uint32_t i2 = mesh->indices[offset + 2u];
                if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;
                const Models::Vertex& v0 = mesh->vertices[i0];
                const Models::Vertex& v1 = mesh->vertices[i1];
                const Models::Vertex& v2 = mesh->vertices[i2];
                const Models::Vec3 p0_local = pose ? skinned_positions[i0] : v0.position;
                const Models::Vec3 p1_local = pose ? skinned_positions[i1] : v1.position;
                const Models::Vec3 p2_local = pose ? skinned_positions[i2] : v2.position;
                const Models::Vec3 n0_local = pose ? skinned_normals[i0] : v0.normal;
                const Models::Vec3 n1_local = pose ? skinned_normals[i1] : v1.normal;
                const Models::Vec3 n2_local = pose ? skinned_normals[i2] : v2.normal;

                const Vec3f p0 = transformPoint(model, {p0_local.x, p0_local.y, p0_local.z});
                const Vec3f p1 = transformPoint(model, {p1_local.x, p1_local.y, p1_local.z});
                const Vec3f p2 = transformPoint(model, {p2_local.x, p2_local.y, p2_local.z});
                const Vec3f n0 = transformNormal(inverse_model, {n0_local.x, n0_local.y, n0_local.z});
                const Vec3f n1 = transformNormal(inverse_model, {n1_local.x, n1_local.y, n1_local.z});
                const Vec3f n2 = transformNormal(inverse_model, {n2_local.x, n2_local.y, n2_local.z});

                AccelTriangle triangle{};
                triangle.p0 = {p0.x, p0.y, p0.z, 0.0f};
                triangle.p1 = {p1.x, p1.y, p1.z, 0.0f};
                triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
                triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
                triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
                triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
                triangle.uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
                triangle.uv2 = {v2.uv.x, v2.uv.y, static_cast<float>(gpu_material), 0.0f};
                triangles.push_back(triangle);
            }
        }

        if (triangles.empty()) {
            std::fprintf(stderr, "[PathTracer]: scene contains no traceable triangles\n");
            return false;
        }
        if (materials.empty()) materials.push_back(GpuMaterial{});

        PathTracerAccel::BuildResult bvh = PathTracerAccel::buildWideBvh(triangles);
        if (!bvh.valid) {
            std::fprintf(stderr, "[PathTracer]: BVH8 build failed: %s\n", bvh.error.c_str());
            return false;
        }

        const bool uploaded =
            uploadBuffer(node_buffer, bvh.nodes.data(), bvh.nodes.size() * sizeof(WideNode), GL_STATIC_DRAW) &&
            uploadBuffer(
                triangle_buffer,
                bvh.triangles.data(),
                bvh.triangles.size() * sizeof(AccelTriangle),
                GL_STATIC_DRAW
            ) &&
            uploadBuffer(material_buffer, materials.data(), materials.size() * sizeof(GpuMaterial), GL_STATIC_DRAW);
        if (!uploaded) return false;

        node_count = bvh.nodes.size();
        triangle_count = bvh.triangles.size();
        material_count = materials.size();
        cache_needs_clear = true;
        history_valid = false;

        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        std::fprintf(
            stderr,
            "[PathTracer]: BVH8 cache %zu triangles, %zu wide nodes, %zu materials, %.2f ms\n",
            triangle_count,
            node_count,
            material_count,
            elapsed
        );
        return true;
    }

    void bindAccel(GLuint program)
    {
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, material_buffer);
        setInt(program, "uNodeCount", static_cast<int>(node_count));
        setInt(program, "uMaterialCount", static_cast<int>(material_count));
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

    void setResolution(GLuint program)
    {
        // lwcgl v2.9.3 does not expose glUniform2i. Resolution uniforms in the
        // new shaders are therefore set through two scalar integer uniforms
        // generated by the shader helper aliases below when present.
        setInt(program, "uResolutionX", render_width);
        setInt(program, "uResolutionY", render_height);
    }

    void resetQueues()
    {
        PathTracerGpu::QueueCounters counters{};
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, counter_buffer);
        GL15.glBufferSubData(
            GL_SHADER_STORAGE_BUFFER,
            0,
            static_cast<LWCGLsizeiptr>(sizeof(counters)),
            &counters
        );
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
    }

    void clearRadianceCache()
    {
        if (!cache_needs_clear) return;
        GL20.glUseProgram(clear_cache_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, radiance_cache);
        setUInt(clear_cache_program, "uCacheSize", static_cast<std::uint32_t>(PathTracerGpu::RADIANCE_CACHE_ENTRIES));
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
        bindAccel(prepare_dispatch_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, counter_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, dispatch_buffer);
        GL43.glDispatchCompute(1u, 1u, 1u);
        GL42.glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        GL20.glUseProgram(0u);
    }

    void dispatch2D(GLuint program)
    {
        GL43.glDispatchCompute(
            static_cast<GLuint>((render_width + 7) / 8),
            static_cast<GLuint>((render_height + 7) / 8),
            1u
        );
    }

    void dispatchFrame(const CameraState& camera, const LightState& light)
    {
        const std::uint32_t pixels = static_cast<std::uint32_t>(pixelCount());
        resetQueues();
        clearRadianceCache();

        GL20.glUseProgram(primary_generate_program);
        bindAccel(primary_generate_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, primary_ray_buffer);
        setUInt(primary_generate_program, "uFrameIndex", frame_index);
        setVec3(primary_generate_program, "uCameraPosition", camera.position);
        setVec3(primary_generate_program, "uCameraForward", camera.forward);
        setVec3(primary_generate_program, "uCameraRight", camera.right);
        setVec3(primary_generate_program, "uCameraUp", camera.up);
        setFloat(primary_generate_program, "uTanHalfFov", std::tan(camera.fov_degrees * (kPi / 360.0f)));
        setFloat(primary_generate_program, "uAspect", static_cast<float>(render_width) / static_cast<float>(render_height));
        setInt(primary_generate_program, "uResolutionX", render_width);
        setInt(primary_generate_program, "uResolutionY", render_height);
        dispatch2D(primary_generate_program);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(primary_intersect_program);
        bindAccel(primary_intersect_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, primary_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, hit_queue_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, counter_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7u, reservoir_work_a);
        setUInt(primary_intersect_program, "uPixelCount", pixels);
        GL43.glDispatchCompute((pixels + 63u) / 64u, 1u, 1u);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        prepareDispatchCommands();

        GL15.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, dispatch_buffer);
        GL20.glUseProgram(primary_shade_program);
        bindAccel(primary_shade_program);
        bindTextures(primary_shade_program);
        setLightUniforms(primary_shade_program, light);
        setUInt(primary_shade_program, "uFrameIndex", frame_index);
        setUInt(primary_shade_program, "uPixelCount", pixels);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, hit_queue_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, counter_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7u, direct_lighting);
        GL43.glDispatchComputeIndirect(0);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        GL15.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0u);

        prepareDispatchCommands();
        GL15.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, dispatch_buffer);

        GL20.glUseProgram(bounce_intersect_program);
        bindAccel(bounce_intersect_program);
        setUInt(bounce_intersect_program, "uPixelCount", pixels);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, bounce_ray_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, secondary_surface);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, counter_buffer);
        GL43.glDispatchComputeIndirect(static_cast<LWCGLintptr>(sizeof(PathTracerGpu::DispatchCommand)));
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(bounce_shade_program);
        bindAccel(bounce_shade_program);
        bindTextures(bounce_shade_program);
        setLightUniforms(bounce_shade_program, light);
        setUInt(bounce_shade_program, "uFrameIndex", frame_index);
        setUInt(bounce_shade_program, "uPixelCount", pixels);
        setUInt(
            bounce_shade_program,
            "uCacheSize",
            static_cast<std::uint32_t>(PathTracerGpu::RADIANCE_CACHE_ENTRIES)
        );
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, secondary_surface);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5u, reservoir_work_a);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6u, radiance_cache);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7u, counter_buffer);
        GL43.glDispatchComputeIndirect(static_cast<LWCGLintptr>(sizeof(PathTracerGpu::DispatchCommand)));
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        GL15.glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0u);

        GL20.glUseProgram(restir_temporal_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, surface_previous);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, reservoir_work_a);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, reservoir_previous);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, reservoir_work_b);
        setInt(restir_temporal_program, "uResolutionX", render_width);
        setInt(restir_temporal_program, "uResolutionY", render_height);
        setUInt(restir_temporal_program, "uFrameIndex", frame_index);
        setInt(restir_temporal_program, "uHistoryValid", history_valid ? 1 : 0);
        setMat4(restir_temporal_program, "uPreviousViewProjection", previous_view_projection);
        setVec3(restir_temporal_program, "uPreviousCameraPosition", previous_camera_position);
        dispatch2D(restir_temporal_program);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(restir_spatial_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, reservoir_work_b);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, reservoir_work_a);
        setInt(restir_spatial_program, "uResolutionX", render_width);
        setInt(restir_spatial_program, "uResolutionY", render_height);
        setUInt(restir_spatial_program, "uFrameIndex", frame_index);
        dispatch2D(restir_spatial_program);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GL20.glUseProgram(compose_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, surface_current);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, reservoir_work_a);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, direct_lighting);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, lighting_current);
        setInt(compose_program, "uResolutionX", render_width);
        setInt(compose_program, "uResolutionY", render_height);
        dispatch2D(compose_program);
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
        dispatch2D(temporal_filter_program);
        GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        GLuint atrous_input = lighting_temporal;
        GLuint atrous_output = lighting_current;
        for (int iteration = 0; iteration < 3; ++iteration) {
            if (iteration == 1) {
                atrous_input = lighting_current;
                atrous_output = direct_lighting;
            } else if (iteration == 2) {
                atrous_input = direct_lighting;
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
            dispatch2D(atrous_program);
            GL42.glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        GL20.glUseProgram(copy_output_program);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, lighting_current);
        GL42.glBindImageTexture(0u, output_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        setInt(copy_output_program, "uResolutionX", render_width);
        setInt(copy_output_program, "uResolutionY", render_height);
        dispatch2D(copy_output_program);
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
        "[PathTracer]: wavefront OpenGL %d.%d, output %dx%d, trace %dx%d, ReSTIR GI + SVGF\n",
        major,
        minor,
        impl_->width,
        impl_->height,
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
        std::fprintf(stderr, "[PathTracer]: failed to resize wavefront frame resources\n");
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
    const Impl::LightState light = impl_->lightState(world);
    const std::uint64_t next_scene_signature = impl_->sceneSignature(world);
    const std::uint64_t next_light_signature = impl_->lightSignature(light);

    if (next_scene_signature != impl_->scene_signature) {
        if (!impl_->syncScene(world)) return;
        impl_->scene_signature = next_scene_signature;
    }
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
    impl_->deleteBuffer(impl_->node_buffer);
    impl_->deleteBuffer(impl_->triangle_buffer);
    impl_->deleteBuffer(impl_->material_buffer);
    for (const auto& [handle, texture] : impl_->texture_cache) {
        (void)handle;
        if (texture != 0u) glDeleteTextures(1, &texture);
    }
    impl_->texture_cache.clear();
    impl_->texture_slots.fill(0u);
    impl_->texture_slot_count = 0u;
    impl_->node_count = 0u;
    impl_->triangle_count = 0u;
    impl_->material_count = 0u;
    impl_->scene_signature = 0u;
    impl_->light_signature = 0u;
    impl_->frame_index = 0u;
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
