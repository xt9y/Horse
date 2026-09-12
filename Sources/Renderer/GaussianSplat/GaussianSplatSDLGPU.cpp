#include "Renderer/GaussianSplat/GaussianSplatSDLGPU.hpp"

#include "Models/GaussianSplat.hpp"
#include "Models/Models.hpp"
#include "Renderer/GaussianSplat/Projection.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/Systems/Scene.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer::GaussianSplat::SDLGPU {
namespace {

constexpr std::uint32_t TileSize = 16u;
constexpr std::uint32_t WorkgroupSize = 8u;

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
    SDL_GPUComputePipeline *pipeline = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    SDL_GPUTexture *output = nullptr;
    SDL_GPUTexture *empty_depth = nullptr;
    SDL_GPUBuffer *splats = nullptr;
    SDL_GPUBuffer *indices = nullptr;
    SDL_GPUBuffer *ranges = nullptr;
    std::size_t splat_capacity = 0u;
    std::size_t index_capacity = 0u;
    std::size_t range_capacity = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t resource_revision = 0u;
    std::unordered_map<Models::MeshHandle, CacheEntry> cache;
};

State state;

inline constexpr const char *Shader = R"HLSL(
struct GpuSplat {
    float4 center_axis0;
    float4 axis1_depth;
    float4 color_opacity;
};
struct GpuRange { uint4 value; };

Texture2D<float4> Source : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
Texture2D<float4> SceneDepth : register(t1, space0);
SamplerState DepthSampler : register(s1, space0);
StructuredBuffer<GpuSplat> Splats : register(t2, space0);
StructuredBuffer<uint> Indices : register(t3, space0);
StructuredBuffer<GpuRange> Ranges : register(t4, space0);

RWTexture2D<float4> Output : register(u0, space1);

cbuffer GaussianUniforms : register(b0, space2) {
    uint4 Params;
};

[numthreads(8, 8, 1)]
void Main(uint3 global_id : SV_DispatchThreadID)
{
    uint2 pixel = global_id.xy;
    uint width = max(Params.x, 1u);
    uint height = max(Params.y, 1u);
    if (pixel.x >= width || pixel.y >= height) return;

    float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 color = Source.SampleLevel(SourceSampler, uv, 0.0f);
    float surface_depth = Params.w != 0u
        ? SceneDepth.SampleLevel(DepthSampler, uv, 0.0f).r
        : 1.0e30f;

    uint tiles_x = max(Params.z, 1u);
    uint tile = (pixel.y / 16u) * tiles_x + pixel.x / 16u;
    uint4 range = Ranges[tile].value;
    for (uint local = 0u; local < range.y; ++local) {
        GpuSplat splat = Splats[Indices[range.x + local]];
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
        if (Params.w != 0u) {
            float epsilon = max(0.0025f, surface_depth * 0.0005f);
            if (splat.axis1_depth.z > surface_depth + epsilon) continue;
        }
        float alpha = saturate(splat.color_opacity.a * exp(-0.5f * radius_squared));
        if (alpha < (1.0f / 255.0f)) continue;
        float3 splat_color = max(splat.color_opacity.rgb, 0.0.xxx);
        color.rgb = splat_color * alpha + color.rgb * (1.0f - alpha);
        color.a = alpha + color.a * (1.0f - alpha);
    }
    Output[pixel] = color;
}
)HLSL";

bool ensureCore()
{
    if (state.pipeline && state.sampler && state.empty_depth) return true;
    if (!Renderer::SDLGPU::device()) return false;

    state.pipeline = Renderer::SDLGPU::compileComputePipeline(
        Shader, "Horse Gaussian Splat", "Main");
    state.sampler = Renderer::SDLGPU::createNearestSampler();

    const std::uint8_t white[4] = {255u, 255u, 255u, 255u};
    state.empty_depth = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        1u, 1u,
        "Horse Gaussian Empty Depth");
    if (state.empty_depth && !Renderer::SDLGPU::uploadTextureRgba8(
            state.empty_depth, 1u, 1u, white, sizeof white))
        return false;

    if (!state.pipeline || !state.sampler || !state.empty_depth) {
        std::fprintf(stderr, "[GaussianSplat/SDL_GPU]: initialization failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

bool ensureOutput(std::uint32_t width, std::uint32_t height)
{
    if (state.output && state.width == width && state.height == height) return true;
    if (state.output)
        SDL_ReleaseGPUTexture(Renderer::SDLGPU::device(), state.output);
    state.output = nullptr;
    state.width = width;
    state.height = height;
    state.output = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(),
        SDL_GPU_TEXTUREUSAGE_SAMPLER |
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
            SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
        width,
        height,
        "Horse Gaussian Output");
    return state.output != nullptr;
}

bool ensureBuffer(
    SDL_GPUBuffer *&buffer,
    std::size_t& capacity,
    std::size_t bytes,
    const char *label)
{
    if (buffer && capacity >= bytes) return true;
    if (buffer)
        SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), buffer);
    buffer = nullptr;
    capacity = 0u;
    const std::size_t next = std::max<std::size_t>(bytes, 256u);
    buffer = Renderer::SDLGPU::createBuffer(
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        next,
        nullptr,
        label);
    if (!buffer) return false;
    capacity = next;
    return true;
}

bool decoded(
    const Systems::Scene::RenderItem& item,
    const Models::GaussianSplat::Data **output,
    std::string *error)
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
        const int max_x = std::min(
            static_cast<int>(width) - 1,
            static_cast<int>(std::ceil(center_x + pixel_radius_x)));
        const int min_y = std::max(0, static_cast<int>(std::floor(center_y - pixel_radius_y)));
        const int max_y = std::min(
            static_cast<int>(height) - 1,
            static_cast<int>(std::ceil(center_y + pixel_radius_y)));
        if (min_x > max_x || min_y > max_y) continue;
        const std::uint32_t tile_min_x = static_cast<std::uint32_t>(min_x) / TileSize;
        const std::uint32_t tile_max_x = static_cast<std::uint32_t>(max_x) / TileSize;
        const std::uint32_t tile_min_y = static_cast<std::uint32_t>(min_y) / TileSize;
        const std::uint32_t tile_max_y = static_cast<std::uint32_t>(max_y) / TileSize;
        for (std::uint32_t y = tile_min_y; y <= tile_max_y; ++y)
            for (std::uint32_t x = tile_min_x; x <= tile_max_x; ++x)
                bins[static_cast<std::size_t>(y) * tiles_x + x].push_back(
                    static_cast<std::uint32_t>(index));
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

    const std::uint32_t width = static_cast<std::uint32_t>(std::max(output.width, 1));
    const std::uint32_t height = static_cast<std::uint32_t>(std::max(output.height, 1));
    if (!ensureOutput(width, height)) return false;

    std::vector<ProjectedSplat> projected;
    for (const Systems::Scene::RenderItem& item : items) {
        if (!item.transform || !item.mesh_component || !item.mesh) continue;
        const Models::GaussianSplat::Data *data = nullptr;
        std::string error;
        if (!decoded(item, &data, &error)) {
            std::fprintf(stderr, "[GaussianSplat/SDL_GPU]: %s\n", error.c_str());
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
    if (projected.size() > std::numeric_limits<std::uint32_t>::max()) return false;

    std::stable_sort(
        projected.begin(), projected.end(),
        [](const ProjectedSplat& a, const ProjectedSplat& b) {
            return a.distance_squared > b.distance_squared;
        });

    std::vector<GpuSplat> gpu_splats;
    std::vector<std::uint32_t> indices;
    std::vector<GpuRange> ranges;
    buildTiles(projected, width, height, &gpu_splats, &indices, &ranges);
    if (gpu_splats.empty() || ranges.empty()) return true;

    const std::size_t splat_bytes = gpu_splats.size() * sizeof(GpuSplat);
    const std::size_t index_bytes = std::max<std::size_t>(
        indices.size() * sizeof(std::uint32_t), sizeof(std::uint32_t));
    const std::size_t range_bytes = ranges.size() * sizeof(GpuRange);
    if (!ensureBuffer(state.splats, state.splat_capacity, splat_bytes, "Horse Gaussian Splats") ||
        !ensureBuffer(state.indices, state.index_capacity, index_bytes, "Horse Gaussian Indices") ||
        !ensureBuffer(state.ranges, state.range_capacity, range_bytes, "Horse Gaussian Ranges"))
        return false;

    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    if (!Renderer::SDLGPU::uploadBuffer(
            command, state.splats, gpu_splats.data(), splat_bytes, true))
        return false;
    const std::uint32_t empty_index = 0u;
    if (!Renderer::SDLGPU::uploadBuffer(
            command,
            state.indices,
            indices.empty() ? static_cast<const void *>(&empty_index)
                            : static_cast<const void *>(indices.data()),
            index_bytes,
            true) ||
        !Renderer::SDLGPU::uploadBuffer(
            command, state.ranges, ranges.data(), range_bytes, true))
        return false;

    const bool has_depth =
        output.depth == Internal::DepthSource::LinearTexture && output.depth_texture;
    const Uniforms uniforms{{
        width,
        height,
        (width + TileSize - 1u) / TileSize,
        has_depth ? 1u : 0u,
    }};
    SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

    SDL_GPUStorageTextureReadWriteBinding writable{};
    writable.texture = state.output;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(command, &writable, 1u, nullptr, 0u);
    if (!pass) {
        std::fprintf(stderr, "[GaussianSplat/SDL_GPU]: compute pass failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_BindGPUComputePipeline(pass, state.pipeline);
    const SDL_GPUTextureSamplerBinding samplers[2] = {
        {static_cast<SDL_GPUTexture *>(output.color_texture), state.sampler},
        {
            has_depth ? static_cast<SDL_GPUTexture *>(output.depth_texture) : state.empty_depth,
            state.sampler,
        },
    };
    SDL_BindGPUComputeSamplers(pass, 0u, samplers, 2u);
    SDL_GPUBuffer *buffers[] = {state.splats, state.indices, state.ranges};
    SDL_BindGPUComputeStorageBuffers(pass, 0u, buffers, 3u);
    SDL_DispatchGPUCompute(
        pass,
        (width + WorkgroupSize - 1u) / WorkgroupSize,
        (height + WorkgroupSize - 1u) / WorkgroupSize,
        1u);
    SDL_EndGPUComputePass(pass);

    output.color_texture = state.output;
    return true;
}

void shutdown()
{
    state.cache.clear();
    state.resource_revision = 0u;
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (state.splats) SDL_ReleaseGPUBuffer(device, state.splats);
        if (state.indices) SDL_ReleaseGPUBuffer(device, state.indices);
        if (state.ranges) SDL_ReleaseGPUBuffer(device, state.ranges);
        if (state.output) SDL_ReleaseGPUTexture(device, state.output);
        if (state.empty_depth) SDL_ReleaseGPUTexture(device, state.empty_depth);
        if (state.sampler) SDL_ReleaseGPUSampler(device, state.sampler);
        if (state.pipeline) SDL_ReleaseGPUComputePipeline(device, state.pipeline);
    }
    state = {};
}

} // namespace Renderer::GaussianSplat::SDLGPU
