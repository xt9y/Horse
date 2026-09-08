#ifdef __APPLE__

#include "Renderer/PathTracer/PathTracer.hpp"

#include "Animation/Animation.hpp"
#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <lwcgl/lwcgl.h>
#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

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
constexpr std::uint32_t kLeafSize = 8u;
constexpr std::size_t kMaximumTriangles = 1000000u;
constexpr std::size_t kMaximumTextureSlots = 16u;

Vec3f subtract(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
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
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {value.x * inverse_length, value.y * inverse_length, value.z * inverse_length};
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

Vec3f transformNormal(const Mat4& world_to_object, const Vec3f& normal)
{
    return normalize({
        world_to_object[0] * normal.x + world_to_object[1] * normal.y + world_to_object[2] * normal.z,
        world_to_object[4] * normal.x + world_to_object[5] * normal.y + world_to_object[6] * normal.z,
        world_to_object[8] * normal.x + world_to_object[9] * normal.y + world_to_object[10] * normal.z,
    });
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

    struct alignas(16) TraceUniforms {
        std::array<float, 4> camera_position{};
        std::array<float, 4> camera_forward{};
        std::array<float, 4> camera_right{};
        std::array<float, 4> camera_up{};
        std::array<float, 4> light_position_intensity{};
        std::array<float, 4> light_color_tan_half_fov{};
        std::array<float, 4> resolution_aspect{};
        std::array<std::int32_t, 4> counts{};
        std::array<std::uint32_t, 4> frame{};
    };

    struct alignas(16) PresentUniforms {
        std::array<float, 4> exposure{};
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
    std::uint32_t frame_index = 0u;
    std::uint32_t phase_count = 0u;
    bool reset_pending = true;
    bool camera_moving = false;
    bool was_camera_moving = false;
    std::uint64_t scene_signature = 0u;
    std::uint64_t camera_signature = 0u;
    std::uint64_t light_signature = 0u;
    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();

    LWMGLLibrary shader_library = nullptr;
    LWMGLFunction trace_function = nullptr;
    LWMGLFunction present_vertex_function = nullptr;
    LWMGLFunction present_fragment_function = nullptr;
    LWMGLComputePipeline trace_pipeline = nullptr;
    LWMGLRenderPipeline present_pipeline = nullptr;
    LWMGLTexture accumulation = nullptr;
    LWMGLTexture white_texture = nullptr;
    LWMGLSampler material_sampler = nullptr;
    LWMGLSampler present_sampler = nullptr;
    LWMGLBuffer node_buffer = nullptr;
    LWMGLBuffer triangle_buffer = nullptr;
    LWMGLBuffer material_buffer = nullptr;
    LWMGLBuffer trace_uniform_buffer = nullptr;
    LWMGLBuffer present_uniform_buffer = nullptr;

    std::unordered_map<std::uint32_t, LWMGLTexture> texture_cache;
    std::vector<GpuNode> gpu_nodes;
    std::vector<GpuTriangle> gpu_triangles;
    std::vector<GpuMaterial> gpu_materials;
    std::array<LWMGLTexture, kMaximumTextureSlots> texture_slots{};
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

    bool createPrograms()
    {
        shader_library = Metal.createLibraryFromSource(
            PathTracerMetalShaders::source,
            std::strlen(PathTracerMetalShaders::source)
        );
        if (!shader_library) return false;

        trace_function = Metal.createFunction(shader_library, "trace_kernel");
        present_vertex_function = Metal.createFunction(shader_library, "present_vertex");
        present_fragment_function = Metal.createFunction(shader_library, "present_fragment");
        if (!trace_function || !present_vertex_function || !present_fragment_function) return false;

        trace_pipeline = Metal.createComputePipeline(trace_function);
        present_pipeline = Metal.createRenderPipeline(
            present_vertex_function,
            present_fragment_function,
            LWMGL_BGRA8_UNORM
        );
        return trace_pipeline && present_pipeline;
    }

    void destroyPrograms()
    {
        if (trace_pipeline) Metal.destroyComputePipeline(trace_pipeline);
        if (present_pipeline) Metal.destroyRenderPipeline(present_pipeline);
        if (trace_function) Metal.destroyFunction(trace_function);
        if (present_vertex_function) Metal.destroyFunction(present_vertex_function);
        if (present_fragment_function) Metal.destroyFunction(present_fragment_function);
        if (shader_library) Metal.destroyLibrary(shader_library);
        trace_pipeline = nullptr;
        present_pipeline = nullptr;
        trace_function = nullptr;
        present_vertex_function = nullptr;
        present_fragment_function = nullptr;
        shader_library = nullptr;
    }

    bool createSamplers()
    {
        const LWMGLSamplerDesc material_desc = {
            LWMGL_FILTER_LINEAR,
            LWMGL_FILTER_LINEAR,
            LWMGL_ADDRESS_REPEAT,
            LWMGL_ADDRESS_REPEAT
        };
        const LWMGLSamplerDesc present_desc = {
            LWMGL_FILTER_LINEAR,
            LWMGL_FILTER_LINEAR,
            LWMGL_ADDRESS_CLAMP,
            LWMGL_ADDRESS_CLAMP
        };
        material_sampler = Metal.createSampler(&material_desc);
        present_sampler = Metal.createSampler(&present_desc);
        return material_sampler && present_sampler;
    }

    void destroySamplers()
    {
        if (material_sampler) Metal.destroySampler(material_sampler);
        if (present_sampler) Metal.destroySampler(present_sampler);
        material_sampler = nullptr;
        present_sampler = nullptr;
    }

    bool createUniformBuffers()
    {
        const LWMGLBufferDesc trace_desc = {sizeof(TraceUniforms), LWMGL_STORAGE_SHARED};
        const LWMGLBufferDesc present_desc = {sizeof(PresentUniforms), LWMGL_STORAGE_SHARED};
        TraceUniforms trace{};
        PresentUniforms present{};
        trace_uniform_buffer = Metal.createBuffer(&trace_desc, &trace);
        present_uniform_buffer = Metal.createBuffer(&present_desc, &present);
        return trace_uniform_buffer && present_uniform_buffer;
    }

    void destroyUniformBuffers()
    {
        if (trace_uniform_buffer) Metal.destroyBuffer(trace_uniform_buffer);
        if (present_uniform_buffer) Metal.destroyBuffer(present_uniform_buffer);
        trace_uniform_buffer = nullptr;
        present_uniform_buffer = nullptr;
    }

    bool createWhiteTexture()
    {
        const LWMGLTextureDesc desc = {
            1u,
            1u,
            LWMGL_RGBA8_UNORM,
            LWMGL_TEXTURE_SAMPLED,
            LWMGL_STORAGE_SHARED
        };
        white_texture = Metal.createTexture(&desc);
        if (!white_texture) return false;
        const std::uint8_t white[4] = {255u, 255u, 255u, 255u};
        if (Metal.uploadTexture2D(white_texture, white, 4u) != 0) return false;
        texture_slots.fill(white_texture);
        return true;
    }

    void destroyWhiteTexture()
    {
        if (white_texture) Metal.destroyTexture(white_texture);
        white_texture = nullptr;
        texture_slots.fill(nullptr);
    }

    bool createAccumulation()
    {
        const LWMGLTextureDesc desc = {
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            LWMGL_RGBA32_FLOAT,
            LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_READ | LWMGL_TEXTURE_WRITE,
            LWMGL_STORAGE_PRIVATE
        };
        accumulation = Metal.createTexture(&desc);
        if (!accumulation) return false;
        resetFrameHistory();
        return true;
    }

    void destroyAccumulation()
    {
        if (accumulation) Metal.destroyTexture(accumulation);
        accumulation = nullptr;
        resetFrameHistory();
    }

    bool replaceBuffer(LWMGLBuffer& target, const void *data, std::size_t bytes)
    {
        std::array<std::uint8_t, 16> zero{};
        const std::size_t safe_bytes = std::max<std::size_t>(bytes, zero.size());
        const LWMGLBufferDesc desc = {safe_bytes, LWMGL_STORAGE_SHARED};
        LWMGLBuffer replacement = Metal.createBuffer(&desc, bytes == 0u ? zero.data() : data);
        if (!replacement) return false;
        if (target) Metal.destroyBuffer(target);
        target = replacement;
        return true;
    }

    void destroySceneBuffers()
    {
        if (node_buffer) Metal.destroyBuffer(node_buffer);
        if (triangle_buffer) Metal.destroyBuffer(triangle_buffer);
        if (material_buffer) Metal.destroyBuffer(material_buffer);
        node_buffer = nullptr;
        triangle_buffer = nullptr;
        material_buffer = nullptr;
    }

    LWMGLTexture textureFor(std::uint32_t handle)
    {
        if (handle == Models::INVALID_TEXTURE) return nullptr;
        const auto found = texture_cache.find(handle);
        if (found != texture_cache.end()) return found->second;

        const Models::TextureAsset *asset = Models::texture(handle);
        if (!asset || asset->image.width <= 0 || asset->image.height <= 0 || asset->image.rgba.empty()) {
            return nullptr;
        }

        const LWMGLTextureDesc desc = {
            static_cast<std::uint32_t>(asset->image.width),
            static_cast<std::uint32_t>(asset->image.height),
            LWMGL_RGBA8_UNORM,
            LWMGL_TEXTURE_SAMPLED,
            LWMGL_STORAGE_SHARED
        };
        LWMGLTexture texture = Metal.createTexture(&desc);
        if (!texture) return nullptr;
        const std::size_t bytes_per_row = static_cast<std::size_t>(asset->image.width) * 4u;
        if (Metal.uploadTexture2D(texture, asset->image.rgba.data(), bytes_per_row) != 0) {
            Metal.destroyTexture(texture);
            return nullptr;
        }

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
                const Animation::AnimatorComponent *animator = world.get<Animation::AnimatorComponent>(binding->animator);
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
        gpu_nodes.clear();
        gpu_triangles.clear();
        gpu_materials.clear();
        texture_slots.fill(white_texture);
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
                        const LWMGLTexture texture = textureFor(material->diffuse_texture);
                        if (texture) {
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

        for (const Ecs::Entity entity : world.entities()) {
            if (gpu_triangles.size() >= kMaximumTriangles) break;

            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (!mesh || mesh->indices.size() < 3u || mesh->vertices.empty()) continue;

            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.5f) continue;

            const std::uint32_t material_index = materialIndex(mesh_component->material);
            const Mat4 model = modelMatrix(*transform);
            const Mat4 world_to_object = inverseModelMatrix(*transform);

            const Animation::Pose *pose = nullptr;
            const Animation::SkinBindingComponent *binding = world.get<Animation::SkinBindingComponent>(entity);
            if (binding && binding->animator != Ecs::INVALID_ENTITY) {
                const Animation::AnimatorComponent *animator = world.get<Animation::AnimatorComponent>(binding->animator);
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
            replaceBuffer(node_buffer, gpu_nodes.data(), gpu_nodes.size() * sizeof(GpuNode)) &&
            replaceBuffer(triangle_buffer, gpu_triangles.data(), gpu_triangles.size() * sizeof(GpuTriangle)) &&
            replaceBuffer(material_buffer, gpu_materials.data(), gpu_materials.size() * sizeof(GpuMaterial));

        if (uploaded) {
            std::fprintf(
                stderr,
                "[PathTracer]: Metal world cache %zu triangles, %zu nodes, %zu materials\n",
                gpu_triangles.size(),
                gpu_nodes.size(),
                gpu_materials.size()
            );
        }
        return uploaded;
    }

    TraceUniforms traceUniforms(const CameraState& camera, const LightState& light) const
    {
        TraceUniforms uniforms{};
        uniforms.camera_position = {camera.position.x, camera.position.y, camera.position.z, 0.0f};
        uniforms.camera_forward = {camera.forward.x, camera.forward.y, camera.forward.z, 0.0f};
        uniforms.camera_right = {camera.right.x, camera.right.y, camera.right.z, 0.0f};
        uniforms.camera_up = {camera.up.x, camera.up.y, camera.up.z, 0.0f};
        uniforms.light_position_intensity = {
            light.position.x,
            light.position.y,
            light.position.z,
            light.intensity
        };
        uniforms.light_color_tan_half_fov = {
            light.color.x,
            light.color.y,
            light.color.z,
            std::tan(camera.fov_degrees * (kPi / 360.0f))
        };
        uniforms.resolution_aspect = {
            static_cast<float>(trace_width),
            static_cast<float>(trace_height),
            static_cast<float>(width) / static_cast<float>(height),
            0.0f
        };
        uniforms.counts = {
            static_cast<std::int32_t>(gpu_nodes.size()),
            static_cast<std::int32_t>(gpu_triangles.size()),
            static_cast<std::int32_t>(gpu_materials.size()),
            std::clamp(settings.max_bounces, 1, 4)
        };
        uniforms.frame = {
            frame_index,
            reset_pending ? 1u : 0u,
            light.valid ? 1u : 0u,
            camera_moving ? 1u : 0u
        };
        return uniforms;
    }

    bool clearDrawable()
    {
        LWMGLCommand command = Metal.begin();
        if (!command) return false;
        const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
        bool ok = Metal.beginRenderToDrawable(command, clear, 1) == 0;
        if (ok) ok = Metal.endEncoding(command) == 0;
        if (ok) ok = Metal.present(command) == 0;
        if (ok) ok = Metal.commit(command) == 0;
        if (ok) ok = Metal.wait(command) == 0;
        Metal.destroyCommand(command);
        return ok;
    }

    bool dispatchAndPresent(const CameraState& camera, const LightState& light)
    {
        if (!node_buffer || !triangle_buffer || !material_buffer || !accumulation) return false;

        const int samples = std::clamp(settings.samples_per_frame, 1, 4);
        if (sample_count > 1000000000u - static_cast<std::uint32_t>(samples)) resetAccumulation();
        if (frame_index > 1000000000u) frame_index &= 3u;

        const TraceUniforms trace_uniforms = traceUniforms(camera, light);
        if (Metal.uploadBuffer(trace_uniform_buffer, 0u, &trace_uniforms, sizeof trace_uniforms) != 0) return false;

        PresentUniforms present_uniforms{};
        present_uniforms.exposure[0] = settings.exposure;
        present_uniforms.exposure[1] = camera_moving ? 1.0f : 0.0f;
        if (Metal.uploadBuffer(present_uniform_buffer, 0u, &present_uniforms, sizeof present_uniforms) != 0) return false;

        LWMGLCommand command = Metal.begin();
        if (!command) return false;
        bool ok = Metal.beginCompute(command) == 0;
        if (ok) ok = Metal.setComputePipeline(command, trace_pipeline) == 0;
        if (ok) ok = Metal.setBuffer(command, node_buffer, 0u, 0u) == 0;
        if (ok) ok = Metal.setBuffer(command, triangle_buffer, 0u, 1u) == 0;
        if (ok) ok = Metal.setBuffer(command, material_buffer, 0u, 2u) == 0;
        if (ok) ok = Metal.setBuffer(command, trace_uniform_buffer, 0u, 3u) == 0;
        if (ok) ok = Metal.setTexture(command, accumulation, 0u) == 0;
        for (std::size_t slot = 0u; ok && slot < texture_slots.size(); ++slot) {
            ok = Metal.setTexture(command, texture_slots[slot] ? texture_slots[slot] : white_texture, static_cast<std::uint32_t>(slot + 1u)) == 0;
        }
        if (ok) ok = Metal.setSampler(command, material_sampler, 0u) == 0;
        if (ok) ok = Metal.dispatch(
            command,
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            1u
        ) == 0;
        if (ok) ok = Metal.endEncoding(command) == 0;

        const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
        if (ok) ok = Metal.beginRenderToDrawable(command, clear, 1) == 0;
        if (ok) ok = Metal.setRenderPipeline(command, present_pipeline) == 0;
        if (ok) ok = Metal.setFragmentBuffer(command, present_uniform_buffer, 0u, 0u) == 0;
        if (ok) ok = Metal.setFragmentTexture(command, accumulation, 0u) == 0;
        if (ok) ok = Metal.setFragmentSampler(command, present_sampler, 0u) == 0;
        if (ok) ok = Metal.draw(command, 0u, 3u) == 0;
        if (ok) ok = Metal.present(command) == 0;
        if (ok) ok = Metal.commit(command) == 0;
        if (ok) ok = Metal.wait(command) == 0;
        Metal.destroyCommand(command);

        if (!ok) {
            std::fprintf(stderr, "[PathTracer]: Metal frame failed: %s\n", lwmglGetLastError());
            return false;
        }

        sample_count += static_cast<std::uint32_t>(samples);
        ++frame_index;
        phase_count = std::min<std::uint32_t>(phase_count + 1u, 4u);
        reset_pending = false;
        return true;
    }
};

static_assert(sizeof(PathTracer::Impl::GpuNode) == 48u);
static_assert(sizeof(PathTracer::Impl::GpuTriangle) == 128u);
static_assert(sizeof(PathTracer::Impl::GpuMaterial) == 32u);
static_assert(sizeof(PathTracer::Impl::TraceUniforms) == 144u);
static_assert(sizeof(PathTracer::Impl::PresentUniforms) == 16u);

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
    if (!Display.isCreated() || !Display.getNativeWindow()) {
        std::fprintf(stderr, "[PathTracer]: lwcgl Display must be created before Metal PathTracer\n");
        return false;
    }

    if (Metal.create(Display.getNativeWindow()) != 0) {
        std::fprintf(stderr, "[PathTracer]: Metal init failed: %s\n", lwmglGetLastError());
        return false;
    }

    impl_->width = std::max(Display.getWidth(), 1);
    impl_->height = std::max(Display.getHeight(), 1);
    impl_->updateTraceResolution();

    if (
        Metal.resize(static_cast<std::uint32_t>(impl_->width), static_cast<std::uint32_t>(impl_->height)) != 0 ||
        !impl_->createPrograms() ||
        !impl_->createSamplers() ||
        !impl_->createUniformBuffers() ||
        !impl_->createWhiteTexture() ||
        !impl_->createAccumulation())
    {
        std::fprintf(stderr, "[PathTracer]: Metal resource initialization failed: %s\n", lwmglGetLastError());
        shutdown();
        return false;
    }

    impl_->initialized = true;
    LWMGLDeviceInfo info{};
    if (Metal.getDeviceInfo(&info) == 0) {
        std::fprintf(
            stderr,
            "[PathTracer]: Metal %s, output %dx%d, trace %dx%d\n",
            info.name,
            impl_->width,
            impl_->height,
            impl_->trace_width,
            impl_->trace_height
        );
    } else {
        std::fprintf(
            stderr,
            "[PathTracer]: Metal, output %dx%d, trace %dx%d\n",
            impl_->width,
            impl_->height,
            impl_->trace_width,
            impl_->trace_height
        );
    }
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
    if (Metal.resize(static_cast<std::uint32_t>(impl_->width), static_cast<std::uint32_t>(impl_->height)) != 0) {
        std::fprintf(stderr, "[PathTracer]: Metal drawable resize failed: %s\n", lwmglGetLastError());
        shutdown();
        return;
    }

    if (impl_->trace_width == previous_width && impl_->trace_height == previous_height) {
        impl_->resetAccumulation();
        return;
    }

    impl_->destroyAccumulation();
    if (!impl_->createAccumulation()) {
        std::fprintf(stderr, "[PathTracer]: failed to resize Metal accumulation texture: %s\n", lwmglGetLastError());
        shutdown();
    }
}

void PathTracer::render(const Ecs::World& world)
{
    if (!impl_->active()) {
        if (impl_->initialized) impl_->clearDrawable();
        return;
    }

    const int previous_trace_width = impl_->trace_width;
    const int previous_trace_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (impl_->trace_width != previous_trace_width || impl_->trace_height != previous_trace_height) {
        impl_->destroyAccumulation();
        if (!impl_->createAccumulation()) {
            std::fprintf(stderr, "[PathTracer]: failed to recreate Metal accumulation texture: %s\n", lwmglGetLastError());
            shutdown();
            return;
        }
    }

    const Impl::CameraState camera = impl_->cameraState(world);
    if (!camera.valid) {
        impl_->clearDrawable();
        return;
    }

    const Impl::LightState light = impl_->lightState(world);
    const std::uint64_t next_camera_signature = impl_->cameraSignature(camera);
    const std::uint64_t next_light_signature = impl_->lightSignature(light);
    const std::uint64_t next_world_revision = world.changeRevision();

    if (next_world_revision != impl_->world_revision) {
        const std::uint64_t next_scene_signature = impl_->sceneSignature(world);
        if (next_scene_signature != impl_->scene_signature) {
            if (!impl_->syncScene(world)) {
                std::fprintf(stderr, "[PathTracer]: failed to synchronize Metal world cache: %s\n", lwmglGetLastError());
                return;
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

    if (!impl_->dispatchAndPresent(camera, light)) return;
}

void PathTracer::shutdown()
{
    if (!impl_) return;

    if (Metal.isCreated()) Metal.waitIdle();

    for (const auto& [handle, texture] : impl_->texture_cache) {
        (void)handle;
        if (texture) Metal.destroyTexture(texture);
    }
    impl_->texture_cache.clear();

    impl_->destroyAccumulation();
    impl_->destroySceneBuffers();
    impl_->destroyWhiteTexture();
    impl_->destroyUniformBuffers();
    impl_->destroySamplers();
    impl_->destroyPrograms();

    if (Metal.isCreated()) Metal.destroy();

    impl_->gpu_nodes.clear();
    impl_->gpu_triangles.clear();
    impl_->gpu_materials.clear();
    impl_->texture_slots.fill(nullptr);
    impl_->texture_slot_count = 0u;
    impl_->scene_signature = 0u;
    impl_->camera_signature = 0u;
    impl_->light_signature = 0u;
    impl_->world_revision = std::numeric_limits<std::uint64_t>::max();
    impl_->initialized = false;
    impl_->resetFrameHistory();
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

#endif
