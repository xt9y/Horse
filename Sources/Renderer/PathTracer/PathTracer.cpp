#include "Renderer/PathTracer/PathTracer.hpp"

#include "Animation/Animation.hpp"
#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/PathTracerShaders.hpp"

#include <lwcgl/gl11_compat.h>
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
#include <utility>
#include <vector>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer {
namespace {

using Mat4 = std::array<float, 16>;

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kLeafBit = 0x80000000u;
constexpr std::uint32_t kBlasLeafSize = 8u;
constexpr std::uint32_t kTlasLeafSize = 4u;
constexpr std::size_t kMaximumTrianglesPerBlas = 1000000u;
constexpr std::size_t kMaximumTextureSlots = 16u;

Vec3f add(const Vec3f& a, const Vec3f& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f subtract(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f scale(const Vec3f& value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
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
    const float length_squared = dot(value, value);
    if (length_squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    return scale(value, 1.0f / std::sqrt(length_squared));
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
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Mat4 result = identityMatrix();
    result[5] = cosine;
    result[6] = sine;
    result[9] = -sine;
    result[10] = cosine;
    return result;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = cosine;
    result[2] = -sine;
    result[8] = sine;
    result[10] = cosine;
    return result;
}

Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = cosine;
    result[1] = sine;
    result[4] = -sine;
    result[5] = cosine;
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
    const float x = std::abs(transform.scale.x) > 1.0e-8f ? 1.0f / transform.scale.x : 0.0f;
    const float y = std::abs(transform.scale.y) > 1.0e-8f ? 1.0f / transform.scale.y : 0.0f;
    const float z = std::abs(transform.scale.z) > 1.0e-8f ? 1.0f / transform.scale.z : 0.0f;

    return multiply(
        multiply(
            multiply(
                multiply(
                    scaling(x, y, z),
                    rotationZ(-transform.rotation.z)
                ),
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

void setVec2(GLuint program, const char *name, float x, float y)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform2f(location, x, y);
}

void setVec3(GLuint program, const char *name, const Vec3f& value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z);
}

} // namespace

struct PathTracer::Impl {
    struct alignas(16) GpuNode {
        float min_x = 0.0f;
        float min_y = 0.0f;
        float min_z = 0.0f;
        std::uint32_t left = 0u;
        float max_x = 0.0f;
        float max_y = 0.0f;
        float max_z = 0.0f;
        std::uint32_t meta = 0u;
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

    struct alignas(16) GpuInstance {
        Mat4 object_to_world = identityMatrix();
        Mat4 world_to_object = identityMatrix();
        std::array<std::uint32_t, 4> data{};
    };

    struct alignas(16) GpuMaterial {
        std::array<float, 4> base_color {1.0f, 1.0f, 1.0f, 1.0f};
        std::array<std::int32_t, 4> data {-1, 0, 0, 0};
    };

    struct MeshCache {
        std::uint32_t mesh = Models::INVALID_MESH;
        Ecs::Entity owner = Ecs::INVALID_ENTITY;
        std::uint64_t pose_revision = 0u;
        bool dynamic = false;
        Vec3f bounds_min{};
        Vec3f bounds_max{};
        std::vector<GpuNode> nodes;
        std::vector<GpuTriangle> triangles;
    };

    struct InstanceEntry {
        GpuInstance gpu;
        Vec3f bounds_min{};
        Vec3f bounds_max{};
        Vec3f centroid{};
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

    PathTracerSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    std::uint32_t sample_count = 0u;
    std::uint64_t scene_signature = 0u;
    std::uint64_t camera_signature = 0u;
    std::uint64_t light_signature = 0u;

    GLuint trace_program = 0u;
    GLuint present_program = 0u;
    GLuint accumulation = 0u;
    GLuint blas_node_buffer = 0u;
    GLuint triangle_buffer = 0u;
    GLuint tlas_node_buffer = 0u;
    GLuint instance_buffer = 0u;
    GLuint material_buffer = 0u;

    std::unordered_map<std::uint32_t, MeshCache> static_blas;
    std::unordered_map<Ecs::Entity, MeshCache> dynamic_blas;
    std::unordered_map<std::uint32_t, GLuint> texture_cache;

    std::vector<GpuNode> gpu_blas_nodes;
    std::vector<GpuTriangle> gpu_triangles;
    std::vector<GpuNode> gpu_tlas_nodes;
    std::vector<GpuInstance> gpu_instances;
    std::vector<GpuMaterial> gpu_materials;
    std::array<GLuint, kMaximumTextureSlots> texture_slots{};
    std::size_t texture_slot_count = 0u;

    bool active() const
    {
        return initialized && settings.enabled;
    }

    void resetAccumulation()
    {
        sample_count = 0u;
    }

    void updateTraceResolution()
    {
        const int divisor = std::clamp(settings.resolution_divisor, 1, 4);
        trace_width = std::max(width / divisor, 1);
        trace_height = std::max(height / divisor, 1);
    }

    bool createAccumulation()
    {
        GLuint texture = 0u;
        glGenTextures(1, &texture);
        if (texture == 0u) return false;

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

        accumulation = texture;
        resetAccumulation();
        return true;
    }

    void destroyAccumulation()
    {
        if (accumulation != 0u) glDeleteTextures(1, &accumulation);
        accumulation = 0u;
        resetAccumulation();
    }

    bool createPrograms()
    {
        trace_program = createComputeProgram(PathTracerShaders::trace);
        present_program = createGraphicsProgram(
            PathTracerShaders::present_vertex,
            PathTracerShaders::present_fragment
        );
        return trace_program != 0u && present_program != 0u;
    }

    void destroyPrograms()
    {
        if (!GL20.glDeleteProgram) return;
        if (trace_program != 0u) GL20.glDeleteProgram(trace_program);
        if (present_program != 0u) GL20.glDeleteProgram(present_program);
        trace_program = 0u;
        present_program = 0u;
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
        GLuint buffers[] = {
            blas_node_buffer,
            triangle_buffer,
            tlas_node_buffer,
            instance_buffer,
            material_buffer,
        };
        for (GLuint buffer : buffers) {
            if (buffer != 0u) GL15.glDeleteBuffers(1, &buffer);
        }
        blas_node_buffer = 0u;
        triangle_buffer = 0u;
        tlas_node_buffer = 0u;
        instance_buffer = 0u;
        material_buffer = 0u;
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

    std::uint32_t buildBlasNode(MeshCache& cache, std::uint32_t start, std::uint32_t count)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3f bounds_min{infinity, infinity, infinity};
        Vec3f bounds_max{-infinity, -infinity, -infinity};
        Vec3f centroid_min{infinity, infinity, infinity};
        Vec3f centroid_max{-infinity, -infinity, -infinity};

        for (std::uint32_t index = 0u; index < count; ++index) {
            const GpuTriangle& triangle = cache.triangles[start + index];
            const Vec3f p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
            const Vec3f p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
            const Vec3f p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
            bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
            bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
            const Vec3f centroid = triangleCentroid(triangle);
            centroid_min = minVec(centroid_min, centroid);
            centroid_max = maxVec(centroid_max, centroid);
        }

        const std::uint32_t node_index = static_cast<std::uint32_t>(cache.nodes.size());
        cache.nodes.push_back({
            bounds_min.x, bounds_min.y, bounds_min.z, 0u,
            bounds_max.x, bounds_max.y, bounds_max.z, 0u,
        });

        const Vec3f extent = subtract(centroid_max, centroid_min);
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > component(extent, axis)) axis = 2;

        if (count <= kBlasLeafSize || component(extent, axis) <= 1.0e-6f) {
            cache.nodes[node_index].left = start;
            cache.nodes[node_index].meta = kLeafBit | count;
            return node_index;
        }

        const std::uint32_t left_count = count / 2u;
        const std::uint32_t middle = start + left_count;
        std::nth_element(
            cache.triangles.begin() + start,
            cache.triangles.begin() + middle,
            cache.triangles.begin() + start + count,
            [axis](const GpuTriangle& a, const GpuTriangle& b) {
                return component(triangleCentroid(a), axis) < component(triangleCentroid(b), axis);
            }
        );

        const std::uint32_t left = buildBlasNode(cache, start, left_count);
        const std::uint32_t right = buildBlasNode(cache, middle, count - left_count);
        cache.nodes[node_index].left = left;
        cache.nodes[node_index].meta = right;
        return node_index;
    }

    bool buildMeshCache(
        MeshCache& cache,
        std::uint32_t mesh_handle,
        Ecs::Entity owner,
        const Animation::Pose *pose)
    {
        const Models::MeshData *mesh = Models::mesh(mesh_handle);
        if (!mesh || mesh->indices.size() < 3u) return false;

        cache = {};
        cache.mesh = mesh_handle;
        cache.owner = owner;
        cache.dynamic = pose != nullptr;
        cache.pose_revision = pose ? pose->revision : 0u;

        std::vector<Models::Vec3> positions;
        std::vector<Models::Vec3> normals;
        if (pose) {
            positions.resize(mesh->vertices.size());
            normals.resize(mesh->vertices.size());
            for (std::size_t index = 0u; index < mesh->vertices.size(); ++index) {
                const Models::Vertex& vertex = mesh->vertices[index];
                Animation::Vec3 input_position {vertex.position.x, vertex.position.y, vertex.position.z};
                Animation::Vec3 input_normal {vertex.normal.x, vertex.normal.y, vertex.normal.z};
                Animation::Vec3 output_position {};
                Animation::Vec3 output_normal {};
                Animation::skinVertex(
                    *pose,
                    vertex.skin,
                    input_position,
                    input_normal,
                    &output_position,
                    &output_normal
                );
                positions[index] = {output_position.x, output_position.y, output_position.z};
                normals[index] = {output_normal.x, output_normal.y, output_normal.z};
            }
        }

        const std::size_t triangle_count = std::min<std::size_t>(
            mesh->indices.size() / 3u,
            kMaximumTrianglesPerBlas
        );
        cache.triangles.reserve(triangle_count);

        const float infinity = std::numeric_limits<float>::infinity();
        cache.bounds_min = {infinity, infinity, infinity};
        cache.bounds_max = {-infinity, -infinity, -infinity};

        for (std::size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
            const std::size_t offset = triangle_index * 3u;
            const std::uint32_t i0 = mesh->indices[offset + 0u];
            const std::uint32_t i1 = mesh->indices[offset + 1u];
            const std::uint32_t i2 = mesh->indices[offset + 2u];
            if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;

            const Models::Vertex& v0 = mesh->vertices[i0];
            const Models::Vertex& v1 = mesh->vertices[i1];
            const Models::Vertex& v2 = mesh->vertices[i2];
            const Models::Vec3 p0 = pose ? positions[i0] : v0.position;
            const Models::Vec3 p1 = pose ? positions[i1] : v1.position;
            const Models::Vec3 p2 = pose ? positions[i2] : v2.position;
            const Models::Vec3 n0 = pose ? normals[i0] : v0.normal;
            const Models::Vec3 n1 = pose ? normals[i1] : v1.normal;
            const Models::Vec3 n2 = pose ? normals[i2] : v2.normal;

            GpuTriangle triangle{};
            triangle.p0 = {p0.x, p0.y, p0.z, 0.0f};
            triangle.p1 = {p1.x, p1.y, p1.z, 0.0f};
            triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
            triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
            triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
            triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
            triangle.uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
            triangle.uv2 = {v2.uv.x, v2.uv.y, 0.0f, 0.0f};
            cache.triangles.push_back(triangle);

            const Vec3f a{p0.x, p0.y, p0.z};
            const Vec3f b{p1.x, p1.y, p1.z};
            const Vec3f c{p2.x, p2.y, p2.z};
            cache.bounds_min = minVec(cache.bounds_min, minVec(a, minVec(b, c)));
            cache.bounds_max = maxVec(cache.bounds_max, maxVec(a, maxVec(b, c)));
        }

        if (cache.triangles.empty()) return false;
        cache.nodes.reserve(cache.triangles.size() * 2u);
        buildBlasNode(cache, 0u, static_cast<std::uint32_t>(cache.triangles.size()));
        return true;
    }

    MeshCache *staticCache(std::uint32_t mesh_handle)
    {
        auto found = static_blas.find(mesh_handle);
        if (found != static_blas.end()) return &found->second;

        MeshCache cache;
        if (!buildMeshCache(cache, mesh_handle, Ecs::INVALID_ENTITY, nullptr)) return nullptr;
        auto [inserted, ok] = static_blas.emplace(mesh_handle, std::move(cache));
        return ok ? &inserted->second : nullptr;
    }

    MeshCache *dynamicCache(
        Ecs::Entity entity,
        std::uint32_t mesh_handle,
        const Animation::Pose& pose)
    {
        auto found = dynamic_blas.find(entity);
        if (
            found != dynamic_blas.end() &&
            found->second.mesh == mesh_handle &&
            found->second.pose_revision == pose.revision)
        {
            return &found->second;
        }

        MeshCache cache;
        if (!buildMeshCache(cache, mesh_handle, entity, &pose)) return nullptr;
        dynamic_blas[entity] = std::move(cache);
        return &dynamic_blas[entity];
    }

    static Vec3f transformedBoundsMin(const MeshCache& cache, const Mat4& model)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3f result{infinity, infinity, infinity};
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    const Vec3f corner {
                        x == 0 ? cache.bounds_min.x : cache.bounds_max.x,
                        y == 0 ? cache.bounds_min.y : cache.bounds_max.y,
                        z == 0 ? cache.bounds_min.z : cache.bounds_max.z,
                    };
                    result = minVec(result, transformPoint(model, corner));
                }
            }
        }
        return result;
    }

    static Vec3f transformedBoundsMax(const MeshCache& cache, const Mat4& model)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3f result{-infinity, -infinity, -infinity};
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    const Vec3f corner {
                        x == 0 ? cache.bounds_min.x : cache.bounds_max.x,
                        y == 0 ? cache.bounds_min.y : cache.bounds_max.y,
                        z == 0 ? cache.bounds_min.z : cache.bounds_max.z,
                    };
                    result = maxVec(result, transformPoint(model, corner));
                }
            }
        }
        return result;
    }

    std::uint32_t buildTlasNode(
        std::vector<InstanceEntry>& entries,
        std::uint32_t start,
        std::uint32_t count)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3f bounds_min{infinity, infinity, infinity};
        Vec3f bounds_max{-infinity, -infinity, -infinity};
        Vec3f centroid_min{infinity, infinity, infinity};
        Vec3f centroid_max{-infinity, -infinity, -infinity};

        for (std::uint32_t index = 0u; index < count; ++index) {
            const InstanceEntry& entry = entries[start + index];
            bounds_min = minVec(bounds_min, entry.bounds_min);
            bounds_max = maxVec(bounds_max, entry.bounds_max);
            centroid_min = minVec(centroid_min, entry.centroid);
            centroid_max = maxVec(centroid_max, entry.centroid);
        }

        const std::uint32_t node_index = static_cast<std::uint32_t>(gpu_tlas_nodes.size());
        gpu_tlas_nodes.push_back({
            bounds_min.x, bounds_min.y, bounds_min.z, 0u,
            bounds_max.x, bounds_max.y, bounds_max.z, 0u,
        });

        const Vec3f extent = subtract(centroid_max, centroid_min);
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > component(extent, axis)) axis = 2;

        if (count <= kTlasLeafSize || component(extent, axis) <= 1.0e-6f) {
            gpu_tlas_nodes[node_index].left = start;
            gpu_tlas_nodes[node_index].meta = kLeafBit | count;
            return node_index;
        }

        const std::uint32_t left_count = count / 2u;
        const std::uint32_t middle = start + left_count;
        std::nth_element(
            entries.begin() + start,
            entries.begin() + middle,
            entries.begin() + start + count,
            [axis](const InstanceEntry& a, const InstanceEntry& b) {
                return component(a.centroid, axis) < component(b.centroid, axis);
            }
        );

        const std::uint32_t left = buildTlasNode(entries, start, left_count);
        const std::uint32_t right = buildTlasNode(entries, middle, count - left_count);
        gpu_tlas_nodes[node_index].left = left;
        gpu_tlas_nodes[node_index].meta = right;
        return node_index;
    }

    std::uint64_t sceneSignature(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh || !transform) continue;

            hashValue(hash, entity);
            hashValue(hash, mesh->mesh);
            hashValue(hash, mesh->material);
            hashTransform(hash, *transform);

            const Animation::SkinBindingComponent *binding = world.get<Animation::SkinBindingComponent>(entity);
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
        const Vec3f forward{forward_value.x, forward_value.y, forward_value.z};
        const Vec3f right{right_value.x, right_value.y, right_value.z};

        state.valid = true;
        state.position = {transform->position.x, transform->position.y, transform->position.z};
        state.forward = normalize(forward);
        state.right = normalize(right);
        state.up = normalize(cross(state.right, state.forward));
        state.fov_degrees = std::clamp(camera->fov_degrees, 1.0f, 179.0f);
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

    bool syncScene(const Ecs::World& world)
    {
        struct Item {
            const MeshCache *cache = nullptr;
            Transform transform{};
            std::uint32_t material = Models::INVALID_MATERIAL;
        };

        std::vector<Item> items;
        items.reserve(world.entities().size());

        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.5f) continue;

            const Animation::Pose *pose = nullptr;
            const Animation::SkinBindingComponent *binding = world.get<Animation::SkinBindingComponent>(entity);
            if (binding && binding->animator != Ecs::INVALID_ENTITY) {
                const Animation::AnimatorComponent *animator =
                    world.get<Animation::AnimatorComponent>(binding->animator);
                if (animator && !animator->pose.skin.empty()) pose = &animator->pose;
            }

            MeshCache *cache = pose
                ? dynamicCache(entity, mesh_component->mesh, *pose)
                : staticCache(mesh_component->mesh);
            if (!cache || cache->nodes.empty() || cache->triangles.empty()) continue;

            items.push_back({cache, *transform, mesh_component->material});
        }

        gpu_blas_nodes.clear();
        gpu_triangles.clear();
        gpu_tlas_nodes.clear();
        gpu_instances.clear();
        gpu_materials.clear();
        texture_slots.fill(0u);
        texture_slot_count = 0u;

        struct Offsets {
            std::uint32_t nodes = 0u;
            std::uint32_t triangles = 0u;
        };
        std::unordered_map<const MeshCache *, Offsets> offsets;
        offsets.reserve(items.size());

        for (const Item& item : items) {
            if (offsets.find(item.cache) != offsets.end()) continue;
            Offsets value;
            value.nodes = static_cast<std::uint32_t>(gpu_blas_nodes.size());
            value.triangles = static_cast<std::uint32_t>(gpu_triangles.size());
            gpu_blas_nodes.insert(gpu_blas_nodes.end(), item.cache->nodes.begin(), item.cache->nodes.end());
            gpu_triangles.insert(gpu_triangles.end(), item.cache->triangles.begin(), item.cache->triangles.end());
            offsets.emplace(item.cache, value);
        }

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
                    auto texture_found = texture_indices.find(material->diffuse_texture);
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

        std::vector<InstanceEntry> entries;
        entries.reserve(items.size());

        for (const Item& item : items) {
            const auto offset = offsets.find(item.cache);
            if (offset == offsets.end()) continue;

            const Mat4 model = modelMatrix(item.transform);
            const Mat4 inverse_model = inverseModelMatrix(item.transform);
            InstanceEntry entry;
            entry.gpu.object_to_world = model;
            entry.gpu.world_to_object = inverse_model;
            entry.gpu.data = {
                offset->second.nodes,
                offset->second.triangles,
                static_cast<std::uint32_t>(item.cache->nodes.size()),
                materialIndex(item.material),
            };
            entry.bounds_min = transformedBoundsMin(*item.cache, model);
            entry.bounds_max = transformedBoundsMax(*item.cache, model);
            entry.centroid = scale(add(entry.bounds_min, entry.bounds_max), 0.5f);
            entries.push_back(entry);
        }

        if (gpu_materials.empty()) gpu_materials.push_back(GpuMaterial{});

        if (!entries.empty()) {
            gpu_tlas_nodes.reserve(entries.size() * 2u);
            buildTlasNode(entries, 0u, static_cast<std::uint32_t>(entries.size()));
            gpu_instances.reserve(entries.size());
            for (const InstanceEntry& entry : entries) gpu_instances.push_back(entry.gpu);
        }

        const bool uploaded =
            uploadBuffer(
                blas_node_buffer,
                gpu_blas_nodes.data(),
                gpu_blas_nodes.size() * sizeof(GpuNode),
                GL_STATIC_DRAW
            ) &&
            uploadBuffer(
                triangle_buffer,
                gpu_triangles.data(),
                gpu_triangles.size() * sizeof(GpuTriangle),
                GL_STATIC_DRAW
            ) &&
            uploadBuffer(
                tlas_node_buffer,
                gpu_tlas_nodes.data(),
                gpu_tlas_nodes.size() * sizeof(GpuNode),
                GL_DYNAMIC_DRAW
            ) &&
            uploadBuffer(
                instance_buffer,
                gpu_instances.data(),
                gpu_instances.size() * sizeof(GpuInstance),
                GL_DYNAMIC_DRAW
            ) &&
            uploadBuffer(
                material_buffer,
                gpu_materials.data(),
                gpu_materials.size() * sizeof(GpuMaterial),
                GL_DYNAMIC_DRAW
            );

        if (uploaded) {
            std::fprintf(
                stderr,
                "[PathTracer]: scene cache %zu triangles, %zu BLAS nodes, %zu instances, %zu TLAS nodes, %zu materials\n",
                gpu_triangles.size(),
                gpu_blas_nodes.size(),
                gpu_instances.size(),
                gpu_tlas_nodes.size(),
                gpu_materials.size()
            );
        }
        return uploaded;
    }

    void bindTextures()
    {
        for (std::size_t slot = 0u; slot < texture_slots.size(); ++slot) {
            GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
            glBindTexture(GL_TEXTURE_2D, texture_slots[slot]);

            char name[32]{};
            std::snprintf(name, sizeof(name), "uTexture%zu", slot);
            setInt(trace_program, name, static_cast<int>(slot));
        }
        GLModern.glActiveTexture(GL_TEXTURE0);
    }

    void dispatch(const CameraState& camera, const LightState& light)
    {
        const int samples = std::clamp(settings.samples_per_frame, 1, 4);
        if (sample_count > 1000000000u - static_cast<std::uint32_t>(samples)) resetAccumulation();

        GL20.glUseProgram(trace_program);
        setVec2(
            trace_program,
            "uResolution",
            static_cast<float>(trace_width),
            static_cast<float>(trace_height)
        );
        setVec3(trace_program, "uCameraPosition", camera.position);
        setVec3(trace_program, "uCameraForward", camera.forward);
        setVec3(trace_program, "uCameraRight", camera.right);
        setVec3(trace_program, "uCameraUp", camera.up);
        setFloat(trace_program, "uTanHalfFov", std::tan(camera.fov_degrees * (kPi / 360.0f)));
        setFloat(trace_program, "uAspect", static_cast<float>(width) / static_cast<float>(height));
        setInt(trace_program, "uTlasNodeCount", static_cast<int>(gpu_tlas_nodes.size()));
        setInt(trace_program, "uInstanceCount", static_cast<int>(gpu_instances.size()));
        setInt(trace_program, "uMaterialCount", static_cast<int>(gpu_materials.size()));
        setInt(trace_program, "uSamplesThisFrame", samples);
        setInt(trace_program, "uSampleBase", static_cast<int>(sample_count));
        setInt(trace_program, "uMaxBounces", std::clamp(settings.max_bounces, 1, 8));
        setInt(trace_program, "uHasLight", light.valid ? 1 : 0);
        setVec3(trace_program, "uLightPosition", light.position);
        setVec3(trace_program, "uLightColor", light.color);
        setFloat(trace_program, "uLightIntensity", light.intensity);

        bindTextures();
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, blas_node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2u, tlas_node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3u, instance_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4u, material_buffer);
        GL42.glBindImageTexture(0u, accumulation, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);

        GL43.glDispatchCompute(
            static_cast<GLuint>((trace_width + 7) / 8),
            static_cast<GLuint>((trace_height + 7) / 8),
            1u
        );
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        GL20.glUseProgram(0u);

        sample_count += static_cast<std::uint32_t>(samples);
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
        glBindTexture(GL_TEXTURE_2D, accumulation);
        setInt(present_program, "uAccumulation", 0);
        setFloat(present_program, "uSampleCount", static_cast<float>(std::max(sample_count, 1u)));
        setFloat(present_program, "uExposure", settings.exposure);

        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(3.0f, -1.0f);
        glVertex2f(-1.0f, 3.0f);
        glEnd();

        GL20.glUseProgram(0u);
        GLModern.glActiveTexture(GL_TEXTURE0);
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
    if (!impl_->createPrograms() || !impl_->createAccumulation()) {
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

    impl_->destroyAccumulation();
    if (!impl_->createAccumulation()) {
        std::fprintf(stderr, "[PathTracer]: failed to resize accumulation buffer\n");
        shutdown();
    }
}

void PathTracer::render(const Ecs::World& world)
{
    if (!impl_->active()) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const int previous_trace_width = impl_->trace_width;
    const int previous_trace_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (
        impl_->trace_width != previous_trace_width ||
        impl_->trace_height != previous_trace_height)
    {
        impl_->destroyAccumulation();
        if (!impl_->createAccumulation()) {
            std::fprintf(stderr, "[PathTracer]: failed to recreate accumulation buffer\n");
            shutdown();
            return;
        }
    }

    const Impl::CameraState camera = impl_->cameraState(world);
    if (!camera.valid) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    const Impl::LightState light = impl_->lightState(world);
    const std::uint64_t next_scene_signature = impl_->sceneSignature(world);
    const std::uint64_t next_camera_signature = impl_->cameraSignature(camera);
    const std::uint64_t next_light_signature = impl_->lightSignature(light);

    if (next_scene_signature != impl_->scene_signature) {
        if (!impl_->syncScene(world)) {
            std::fprintf(stderr, "[PathTracer]: failed to synchronize scene cache\n");
            return;
        }
        impl_->scene_signature = next_scene_signature;
        impl_->resetAccumulation();
    }

    if (next_camera_signature != impl_->camera_signature) {
        impl_->camera_signature = next_camera_signature;
        impl_->resetAccumulation();
    }

    if (next_light_signature != impl_->light_signature) {
        impl_->light_signature = next_light_signature;
        impl_->resetAccumulation();
    }

    impl_->dispatch(camera, light);
    impl_->present();
}

void PathTracer::shutdown()
{
    if (!impl_) return;

    if (GL20.glUseProgram) GL20.glUseProgram(0u);
    impl_->destroyPrograms();
    impl_->destroyAccumulation();
    impl_->destroyBuffers();

    for (const auto& [handle, texture] : impl_->texture_cache) {
        (void)handle;
        if (texture != 0u) glDeleteTextures(1, &texture);
    }

    impl_->texture_cache.clear();
    impl_->static_blas.clear();
    impl_->dynamic_blas.clear();
    impl_->gpu_blas_nodes.clear();
    impl_->gpu_triangles.clear();
    impl_->gpu_tlas_nodes.clear();
    impl_->gpu_instances.clear();
    impl_->gpu_materials.clear();
    impl_->texture_slots.fill(0u);
    impl_->texture_slot_count = 0u;
    impl_->scene_signature = 0u;
    impl_->camera_signature = 0u;
    impl_->light_signature = 0u;
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
