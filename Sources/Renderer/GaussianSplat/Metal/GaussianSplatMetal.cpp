#ifdef __APPLE__

#include "Renderer/GaussianSplat/Metal/GaussianSplatMetal.hpp"

#include "Models/GaussianSplat.hpp"
#include "Models/Models.hpp"
#include "Renderer/GaussianSplat/Projection.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"

#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer::GaussianSplat::Metal {
namespace {

constexpr std::uint32_t TileSize = 16u;

struct alignas(16) GpuSplat {
    std::array<float, 4> center_axis0{};
    std::array<float, 4> axis1_depth{};
    std::array<float, 4> color_opacity{};
};

struct alignas(16) GpuRange {
    std::array<std::uint32_t, 4> value{};
};

struct alignas(16) Uniforms {
    std::array<std::uint32_t, 4> params{};
};

struct CacheEntry {
    Models::GaussianSplat::Data data;
};

struct State {
    LWMGLLibrary library = nullptr;
    LWMGLFunction function = nullptr;
    LWMGLComputePipeline pipeline = nullptr;
    LWMGLSampler sampler = nullptr;
    LWMGLTexture output = nullptr;
    LWMGLTexture empty_depth = nullptr;
    LWMGLBuffer splats = nullptr;
    LWMGLBuffer indices = nullptr;
    LWMGLBuffer ranges = nullptr;
    LWMGLBuffer uniforms = nullptr;
    std::size_t splat_capacity = 0u;
    std::size_t index_capacity = 0u;
    std::size_t range_capacity = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t resource_revision = 0u;
    std::unordered_map<Models::MeshHandle, CacheEntry> cache;
};

State state;

inline constexpr const char *ShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct GpuSplat {
    float4 center_axis0;
    float4 axis1_depth;
    float4 color_opacity;
};
struct GpuRange { uint4 value; };
struct Uniforms { uint4 params; };

kernel void gaussian_composite(
    device const GpuSplat *splats [[buffer(0)]],
    device const uint *indices [[buffer(1)]],
    device const GpuRange *ranges [[buffer(2)]],
    constant Uniforms& uniforms [[buffer(3)]],
    texture2d<float> source [[texture(0)]],
    texture2d<float, access::write> output [[texture(1)]],
    texture2d<float> scene_depth [[texture(2)]],
    sampler nearest_sampler [[sampler(0)]],
    uint2 pixel [[thread_position_in_grid]])
{
    uint width = max(uniforms.params.x, 1u);
    uint height = max(uniforms.params.y, 1u);
    if (pixel.x >= width || pixel.y >= height) return;

    float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 color = source.sample(nearest_sampler, uv);
    float surface_depth = uniforms.params.w != 0u
        ? scene_depth.sample(nearest_sampler, uv).r
        : 1.0e30f;

    uint tiles_x = max(uniforms.params.z, 1u);
    uint tile = (pixel.y / 16u) * tiles_x + pixel.x / 16u;
    uint4 range = ranges[tile].value;
    for (uint local = 0u; local < range.y; ++local) {
        GpuSplat splat = splats[indices[range.x + local]];
        float2 delta = ndc - splat.center_axis0.xy;
        float2 a = splat.center_axis0.zw;
        float2 b = splat.axis1_depth.xy;
        float determinant = a.x * b.y - a.y * b.x;
        if (abs(determinant) <= 1.0e-12f) continue;
        float2 q = float2(
            (delta.x * b.y - delta.y * b.x) / determinant,
            (a.x * delta.y - a.y * delta.x) / determinant
        );
        float radius_squared = dot(q, q);
        if (radius_squared > 9.0f) continue;
        if (uniforms.params.w != 0u) {
            float epsilon = max(0.0025f, surface_depth * 0.0005f);
            if (splat.axis1_depth.z > surface_depth + epsilon) continue;
        }
        float alpha = clamp(splat.color_opacity.a * exp(-0.5f * radius_squared), 0.0f, 1.0f);
        if (alpha < (1.0f / 255.0f)) continue;
        float3 splat_color = max(splat.color_opacity.rgb, float3(0.0f));
        color.rgb = splat_color * alpha + color.rgb * (1.0f - alpha);
        color.a = alpha + color.a * (1.0f - alpha);
    }
    output.write(color, pixel);
}
)MSL";

bool ensureCore()
{
    if (state.pipeline && state.sampler && state.empty_depth && state.uniforms) return true;
    if (!::Metal.isCreated()) return false;
    state.library = ::Metal.createLibraryFromSource(ShaderSource, std::strlen(ShaderSource));
    if (!state.library) return false;
    state.function = ::Metal.createFunction(state.library, "gaussian_composite");
    if (!state.function) return false;
    state.pipeline = ::Metal.createComputePipeline(state.function);
    if (!state.pipeline) return false;

    const LWMGLSamplerDesc sampler_desc = {
        LWMGL_FILTER_NEAREST,
        LWMGL_FILTER_NEAREST,
        LWMGL_ADDRESS_CLAMP,
        LWMGL_ADDRESS_CLAMP,
    };
    state.sampler = ::Metal.createSampler(&sampler_desc);
    if (!state.sampler) return false;

    const LWMGLTextureDesc depth_desc = {
        1u,
        1u,
        LWMGL_RGBA32_FLOAT,
        LWMGL_TEXTURE_SAMPLED,
        LWMGL_STORAGE_SHARED,
    };
    state.empty_depth = ::Metal.createTexture(&depth_desc);
    if (!state.empty_depth) return false;
    const float empty_depth[4] = {1.0e30f, 0.0f, 0.0f, 1.0f};
    if (::Metal.uploadTexture2D(state.empty_depth, empty_depth, sizeof empty_depth) != 0) return false;

    const LWMGLBufferDesc uniform_desc = {sizeof(Uniforms), LWMGL_STORAGE_SHARED};
    state.uniforms = ::Metal.createBuffer(&uniform_desc, nullptr);
    return state.uniforms != nullptr;
}

bool ensureOutput(std::uint32_t width, std::uint32_t height)
{
    if (state.output && state.width == width && state.height == height) return true;
    if (state.output) ::Metal.destroyTexture(state.output);
    state.output = nullptr;
    state.width = width;
    state.height = height;
    const LWMGLTextureDesc desc = {
        width,
        height,
        LWMGL_RGBA16_FLOAT,
        LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE,
        LWMGL_STORAGE_PRIVATE,
    };
    state.output = ::Metal.createTexture(&desc);
    return state.output != nullptr;
}

bool ensureBuffer(LWMGLBuffer& buffer, std::size_t& capacity, std::size_t bytes)
{
    if (buffer && capacity >= bytes) return true;
    if (buffer) ::Metal.destroyBuffer(buffer);
    buffer = nullptr;
    capacity = 0u;
    const std::size_t next = std::max<std::size_t>(bytes, 256u);
    const LWMGLBufferDesc desc = {next, LWMGL_STORAGE_SHARED};
    buffer = ::Metal.createBuffer(&desc, nullptr);
    if (!buffer) return false;
    capacity = next;
    return true;
}

bool decoded(const Systems::Scene::RenderItem& item, const Models::GaussianSplat::Data **output, std::string *error)
{
    if (!output || !item.mesh || !item.mesh_component) return false;
    const Models::MeshHandle handle = item.mesh_component->mesh;
    auto found = state.cache.find(handle);
    if (found == state.cache.end()) {
        CacheEntry entry;
        if (!Models::GaussianSplat::decode(*item.mesh, &entry.data, error)) return false;
        found = state.cache.emplace(handle, std::move(entry)).first;
    }
    *output = &found->second.data;
    return true;
}

void buildTiles(
    const std::vector<ProjectedSplat>& projected,
    std::uint32_t width,
    std::uint32_t height,
    std::vector<GpuSplat> *gpu_splats,
    std::vector<std::uint32_t> *indices,
    std::vector<GpuRange> *ranges)
{
    const std::uint32_t tiles_x = (width + TileSize - 1u) / TileSize;
    const std::uint32_t tiles_y = (height + TileSize - 1u) / TileSize;
    std::vector<std::vector<std::uint32_t>> bins(
        static_cast<std::size_t>(tiles_x) * static_cast<std::size_t>(tiles_y));

    gpu_splats->resize(projected.size());
    for (std::size_t index = 0u; index < projected.size(); ++index) {
        const ProjectedSplat& source = projected[index];
        (*gpu_splats)[index].center_axis0 = {
            source.center_x, source.center_y, source.axis0_x, source.axis0_y};
        (*gpu_splats)[index].axis1_depth = {
            source.axis1_x, source.axis1_y, source.linear_depth, source.distance_squared};
        (*gpu_splats)[index].color_opacity = {
            source.color.x, source.color.y, source.color.z, source.opacity};

        const float radius_x = 3.0f * (std::abs(source.axis0_x) + std::abs(source.axis1_x));
        const float radius_y = 3.0f * (std::abs(source.axis0_y) + std::abs(source.axis1_y));
        const float center_x = (source.center_x * 0.5f + 0.5f) * static_cast<float>(width);
        const float center_y = (0.5f - source.center_y * 0.5f) * static_cast<float>(height);
        const float pixel_radius_x = radius_x * 0.5f * static_cast<float>(width);
        const float pixel_radius_y = radius_y * 0.5f * static_cast<float>(height);
        const int min_x = std::max(0, static_cast<int>(std::floor(center_x - pixel_radius_x)));
        const int max_x = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(center_x + pixel_radius_x)));
        const int min_y = std::max(0, static_cast<int>(std::floor(center_y - pixel_radius_y)));
        const int max_y = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(center_y + pixel_radius_y)));
        if (min_x > max_x || min_y > max_y) continue;
        const std::uint32_t tile_min_x = static_cast<std::uint32_t>(min_x) / TileSize;
        const std::uint32_t tile_max_x = static_cast<std::uint32_t>(max_x) / TileSize;
        const std::uint32_t tile_min_y = static_cast<std::uint32_t>(min_y) / TileSize;
        const std::uint32_t tile_max_y = static_cast<std::uint32_t>(max_y) / TileSize;
        for (std::uint32_t y = tile_min_y; y <= tile_max_y; ++y)
            for (std::uint32_t x = tile_min_x; x <= tile_max_x; ++x)
                bins[static_cast<std::size_t>(y) * tiles_x + x].push_back(static_cast<std::uint32_t>(index));
    }

    ranges->resize(bins.size());
    indices->clear();
    for (std::size_t tile = 0u; tile < bins.size(); ++tile) {
        (*ranges)[tile].value[0] = static_cast<std::uint32_t>(indices->size());
        (*ranges)[tile].value[1] = static_cast<std::uint32_t>(bins[tile].size());
        indices->insert(indices->end(), bins[tile].begin(), bins[tile].end());
    }
}

} // namespace

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!output.color_texture || !output.command) return true;
    std::vector<Systems::Scene::RenderItem> items;
    Systems::Scene::collectGaussianItems(world, items);
    if (items.empty()) return true;
    if (!ensureCore()) return false;

    const std::uint64_t revision = Models::resourceRevision();
    if (state.resource_revision != revision) {
        state.cache.clear();
        state.resource_revision = revision;
    }

    const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
    if (!camera.valid || camera.projection != Camera::Projection::Perspective) return true;

    const LWMGLTexture source = static_cast<LWMGLTexture>(output.color_texture);
    const std::uint32_t width = ::Metal.textureWidth(source);
    const std::uint32_t height = ::Metal.textureHeight(source);
    if (width == 0u || height == 0u || !ensureOutput(width, height)) return false;

    std::vector<ProjectedSplat> projected;
    for (const Systems::Scene::RenderItem& item : items) {
        if (!item.transform || !item.mesh_component || !item.mesh) continue;
        const Models::GaussianSplat::Data *data = nullptr;
        std::string error;
        if (!decoded(item, &data, &error)) {
            std::fprintf(stderr, "[GaussianSplat/Metal]: %s\n", error.c_str());
            return false;
        }
        const Math::Mat4 model = Math::modelMatrix(*item.transform);
        for (const Models::GaussianSplat::Splat& splat : data->splats) {
            ProjectedSplat value;
            if (Renderer::GaussianSplat::project(
                    splat,
                    data->spherical_harmonic_degree,
                    model,
                    camera,
                    static_cast<int>(width),
                    static_cast<int>(height),
                    &value))
                projected.push_back(value);
        }
    }
    if (projected.empty()) return true;
    std::stable_sort(projected.begin(), projected.end(), [](const ProjectedSplat& a, const ProjectedSplat& b) {
        return a.distance_squared > b.distance_squared;
    });

    std::vector<GpuSplat> gpu_splats;
    std::vector<std::uint32_t> indices;
    std::vector<GpuRange> ranges;
    buildTiles(projected, width, height, &gpu_splats, &indices, &ranges);
    if (gpu_splats.empty() || ranges.empty()) return true;

    const std::size_t splat_bytes = gpu_splats.size() * sizeof(GpuSplat);
    const std::size_t index_bytes = std::max<std::size_t>(indices.size() * sizeof(std::uint32_t), sizeof(std::uint32_t));
    const std::size_t range_bytes = ranges.size() * sizeof(GpuRange);
    if (!ensureBuffer(state.splats, state.splat_capacity, splat_bytes) ||
        !ensureBuffer(state.indices, state.index_capacity, index_bytes) ||
        !ensureBuffer(state.ranges, state.range_capacity, range_bytes))
        return false;
    if (::Metal.uploadBuffer(state.splats, 0u, gpu_splats.data(), splat_bytes) != 0) return false;
    const std::uint32_t empty_index = 0u;
    if (::Metal.uploadBuffer(state.indices, 0u,
            indices.empty() ? static_cast<const void *>(&empty_index) : static_cast<const void *>(indices.data()),
            index_bytes) != 0)
        return false;
    if (::Metal.uploadBuffer(state.ranges, 0u, ranges.data(), range_bytes) != 0) return false;

    const bool has_depth = output.depth == Internal::DepthSource::LinearTexture && output.depth_texture;
    const Uniforms uniforms {{
        width,
        height,
        (width + TileSize - 1u) / TileSize,
        has_depth ? 1u : 0u,
    }};
    if (::Metal.uploadBuffer(state.uniforms, 0u, &uniforms, sizeof uniforms) != 0) return false;

    LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    bool ok = ::Metal.beginCompute(command) == 0;
    if (ok) ok = ::Metal.setComputePipeline(command, state.pipeline) == 0;
    if (ok) ok = ::Metal.setBuffer(command, state.splats, 0u, 0u) == 0;
    if (ok) ok = ::Metal.setBuffer(command, state.indices, 0u, 1u) == 0;
    if (ok) ok = ::Metal.setBuffer(command, state.ranges, 0u, 2u) == 0;
    if (ok) ok = ::Metal.setBuffer(command, state.uniforms, 0u, 3u) == 0;
    if (ok) ok = ::Metal.setTexture(command, source, 0u) == 0;
    if (ok) ok = ::Metal.setTexture(command, state.output, 1u) == 0;
    if (ok) ok = ::Metal.setTexture(command,
        has_depth ? static_cast<LWMGLTexture>(output.depth_texture) : state.empty_depth, 2u) == 0;
    if (ok) ok = ::Metal.setSampler(command, state.sampler, 0u) == 0;
    if (ok) ok = ::Metal.dispatch(command, width, height, 1u) == 0;
    if (ok) ok = ::Metal.endEncoding(command) == 0;
    if (!ok) {
        std::fprintf(stderr, "[GaussianSplat/Metal]: composition failed: %s\n", lwmglGetLastError());
        return false;
    }

    output.color_texture = state.output;
    return true;
}

void shutdown()
{
    state.cache.clear();
    state.resource_revision = 0u;
    if (!::Metal.isCreated()) {
        state = {};
        return;
    }
    if (state.splats) ::Metal.destroyBuffer(state.splats);
    if (state.indices) ::Metal.destroyBuffer(state.indices);
    if (state.ranges) ::Metal.destroyBuffer(state.ranges);
    if (state.uniforms) ::Metal.destroyBuffer(state.uniforms);
    if (state.output) ::Metal.destroyTexture(state.output);
    if (state.empty_depth) ::Metal.destroyTexture(state.empty_depth);
    if (state.sampler) ::Metal.destroySampler(state.sampler);
    if (state.pipeline) ::Metal.destroyComputePipeline(state.pipeline);
    if (state.function) ::Metal.destroyFunction(state.function);
    if (state.library) ::Metal.destroyLibrary(state.library);
    state = {};
}

} // namespace Renderer::GaussianSplat::Metal

#endif
