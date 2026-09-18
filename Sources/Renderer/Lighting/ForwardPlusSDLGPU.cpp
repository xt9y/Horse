#include "Renderer/Internal/ForwardPlusSDLGPU.hpp"

#include "Renderer/Internal/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Renderer::Lighting::SDLGPU {
namespace {

inline constexpr const char *ForwardPlusShader = R"HLSL(
StructuredBuffer<float4> Shading : register(t0, space0);
RWStructuredBuffer<uint> TileCounts : register(u0, space1);
RWStructuredBuffer<uint> TileIndices : register(u1, space1);

cbuffer FrameData : register(b0, space2) {
    float4 CameraPositionNear;
    float4 CameraForwardFar;
    float4 CameraRightAspect;
    float4 CameraUpTanHalfFov;
    float4 ProjectionAlpha;
    float4 Resolution;
    int4 Counts;
    uint4 Frame;
    uint4 PathPolicy;
};

cbuffer ForwardData : register(b1, space2) {
    uint4 GridData;
    uint4 ScreenData;
};

bool ProjectPoint(float3 position, out float2 uv)
{
    float3 delta = position - CameraPositionNear.xyz;
    float depth = dot(delta, CameraForwardFar.xyz);
    if (depth <= max(CameraPositionNear.w, 1.0e-5)) return false;

    float x;
    float y;
    if (ProjectionAlpha.z > 0.5) {
        x = dot(delta, CameraRightAspect.xyz) / max(abs(ProjectionAlpha.x), 1.0e-6);
        y = dot(delta, CameraUpTanHalfFov.xyz) / max(abs(ProjectionAlpha.y), 1.0e-6);
    } else {
        float tan_half_fov = max(CameraUpTanHalfFov.w, 1.0e-6);
        float aspect = max(CameraRightAspect.w, 1.0e-6);
        x = dot(delta, CameraRightAspect.xyz) /
            max(tan_half_fov * aspect * depth, 1.0e-6);
        y = dot(delta, CameraUpTanHalfFov.xyz) /
            max(tan_half_fov * depth, 1.0e-6);
    }

    uv = float2(x * 0.5 + 0.5, 0.5 - y * 0.5);
    return all(isfinite(uv));
}

bool LightAffectsTile(
    uint2 tile,
    float4 position_intensity,
    float4 direction_type,
    float4 color_range)
{
    if (direction_type.w > 1.5 && direction_type.w < 2.5) return true;

    float radius = max(color_range.w, 0.0);
    if (radius <= 0.0) return true;

    float center_depth = dot(
        position_intensity.xyz - CameraPositionNear.xyz,
        CameraForwardFar.xyz
    );
    float near_plane = max(CameraPositionNear.w, 1.0e-5);
    if (center_depth + radius <= near_plane) return false;
    if (center_depth - radius <= near_plane) return true;

    float3 minimum = position_intensity.xyz - radius.xxx;
    float3 maximum = position_intensity.xyz + radius.xxx;
    float3 corners[8] = {
        float3(minimum.x, minimum.y, minimum.z),
        float3(maximum.x, minimum.y, minimum.z),
        float3(maximum.x, maximum.y, minimum.z),
        float3(minimum.x, maximum.y, minimum.z),
        float3(minimum.x, minimum.y, maximum.z),
        float3(maximum.x, minimum.y, maximum.z),
        float3(maximum.x, maximum.y, maximum.z),
        float3(minimum.x, maximum.y, maximum.z)
    };

    float2 uv_min = float2(1.0, 1.0);
    float2 uv_max = float2(0.0, 0.0);
    for (uint corner = 0u; corner < 8u; ++corner) {
        float2 uv;
        if (!ProjectPoint(corners[corner], uv)) return true;
        uv_min = min(uv_min, uv);
        uv_max = max(uv_max, uv);
    }

    if (uv_max.x < 0.0 || uv_max.y < 0.0 || uv_min.x > 1.0 || uv_min.y > 1.0)
        return false;

    float2 screen_size = float2(max(ScreenData.x, 1u), max(ScreenData.y, 1u));
    float2 light_min = saturate(uv_min) * screen_size;
    float2 light_max = saturate(uv_max) * screen_size;
    float tile_size = (float)max(GridData.z, 1u);
    float2 tile_min = float2(tile) * tile_size;
    float2 tile_max = min(tile_min + tile_size.xx, screen_size);

    return light_max.x >= tile_min.x && light_min.x <= tile_max.x &&
        light_max.y >= tile_min.y && light_min.y <= tile_max.y;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= GridData.x) return;

    uint tiles_x = max(GridData.y, 1u);
    uint maximum_lights = max(GridData.w, 1u);
    uint2 tile = uint2(id.x % tiles_x, id.x / tiles_x);
    uint light_count = (uint)max(Shading[11].z, 0.0);
    uint light_base = (uint)max(Shading[11].w, 12.0);

    uint count = 0u;
    bool overflow = false;
    uint output_base = id.x * maximum_lights;

    for (uint light_index = 0u; light_index < light_count; ++light_index) {
        uint offset = light_base + light_index * 5u;
        float4 position_intensity = Shading[offset + 0u];
        float4 direction_type = Shading[offset + 1u];
        float4 color_range = Shading[offset + 2u];
        if (position_intensity.w <= 0.0) continue;
        if (!LightAffectsTile(tile, position_intensity, direction_type, color_range)) continue;

        if (count < maximum_lights)
            TileIndices[output_base + count] = light_index;
        else
            overflow = true;
        ++count;
    }

    TileCounts[id.x] = overflow ? 0xffffffffu : min(count, maximum_lights);
}
)HLSL";

struct alignas(16) ForwardUniforms {
    std::array<std::uint32_t, 4> grid{};
    std::array<std::uint32_t, 4> screen{};
};

struct alignas(16) FragmentUniforms {
    std::array<std::uint32_t, 4> values{};
};

static_assert(sizeof(ForwardUniforms) == 32u);
static_assert(sizeof(FragmentUniforms) == 16u);

bool fail(std::string *error, const char *message)
{
    if (error) *error = message;
    return false;
}

} // namespace

ForwardPlus::~ForwardPlus()
{
    clear();
}

bool ForwardPlus::init()
{
    if (pipeline_) return true;
    if (!Renderer::SDLGPU::device()) return false;
    pipeline_ = Renderer::SDLGPU::compileComputePipeline(
        ForwardPlusShader,
        "Horse Forward+",
        "main"
    );
    return pipeline_ != nullptr;
}

bool ForwardPlus::resize(std::uint32_t width, std::uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    const Renderer::Lighting::ForwardPlus::Grid next =
        Renderer::Lighting::ForwardPlus::grid(width, height);
    if (counts_ && indices_ && width_ == width && height_ == height && grid_ == next)
        return true;

    const std::size_t tile_count =
        static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height);
    if (tile_count == 0u ||
        tile_count > std::numeric_limits<std::size_t>::max() /
            Renderer::Lighting::ForwardPlus::MaximumLightsPerTile)
        return false;

    const std::size_t index_count =
        tile_count * Renderer::Lighting::ForwardPlus::MaximumLightsPerTile;
    if (tile_count > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) ||
        index_count > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t))
        return false;

    SDL_GPUBuffer *next_counts = Renderer::SDLGPU::createBuffer(
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
        tile_count * sizeof(std::uint32_t),
        nullptr,
        "Horse Forward+ Tile Counts"
    );
    if (!next_counts) return false;

    SDL_GPUBuffer *next_indices = Renderer::SDLGPU::createBuffer(
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
            SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
        index_count * sizeof(std::uint32_t),
        nullptr,
        "Horse Forward+ Light Indices"
    );
    if (!next_indices) {
        SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), next_counts);
        return false;
    }

    if (counts_) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), counts_);
    if (indices_) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), indices_);
    counts_ = next_counts;
    indices_ = next_indices;
    width_ = width;
    height_ = height;
    grid_ = next;
    ready_ = false;
    return true;
}

bool ForwardPlus::build(
    SDL_GPUCommandBuffer *command,
    const GlobalIllumination::Field *global_illumination,
    const Renderer::SDLGPU::FrameUniforms& frame,
    std::string *error)
{
    if (error) error->clear();
    ready_ = false;
    if (!command) return fail(error, "missing Forward+ command buffer");
    if (!init()) return fail(error, "failed to initialize Forward+ compute pipeline");
    if (!counts_ || !indices_ || grid_.width == 0u || grid_.height == 0u)
        return fail(error, "Forward+ grid is not allocated");

    const std::uint64_t tile_count_64 =
        static_cast<std::uint64_t>(grid_.width) * static_cast<std::uint64_t>(grid_.height);
    if (tile_count_64 > std::numeric_limits<std::uint32_t>::max())
        return fail(error, "Forward+ tile count exceeds GPU range");
    const std::uint32_t tile_count = static_cast<std::uint32_t>(tile_count_64);

    const ForwardUniforms uniforms{
        {
            tile_count,
            grid_.width,
            Renderer::Lighting::ForwardPlus::TileSize,
            Renderer::Lighting::ForwardPlus::MaximumLightsPerTile,
        },
        {width_, height_, 0u, 0u},
    };
    SDL_PushGPUComputeUniformData(command, 0u, &frame, sizeof(frame));
    SDL_PushGPUComputeUniformData(command, 1u, &uniforms, sizeof(uniforms));

    SDL_GPUStorageBufferReadWriteBinding writable[2]{};
    writable[0].buffer = counts_;
    writable[0].cycle = false;
    writable[1].buffer = indices_;
    writable[1].cycle = false;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
        command,
        nullptr,
        0u,
        writable,
        2u
    );
    if (!pass) return fail(error, "failed to begin Forward+ compute pass");

    SDL_BindGPUComputePipeline(pass, pipeline_);
    if (!Internal::bindGlobalIlluminationSDLGPU(pass, global_illumination, 0u)) {
        SDL_EndGPUComputePass(pass);
        return fail(error, "failed to bind Forward+ shading state");
    }

    SDL_DispatchGPUCompute(pass, (tile_count + 63u) / 64u, 1u, 1u);
    SDL_EndGPUComputePass(pass);
    ready_ = true;
    return true;
}

bool ForwardPlus::bind(
    SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *command,
    bool enabled,
    std::uint32_t slot) const
{
    if (!pass || !command || !counts_ || !indices_) return false;

    SDL_GPUBuffer *buffers[] = {counts_, indices_};
    SDL_BindGPUFragmentStorageBuffers(pass, slot, buffers, 2u);

    const FragmentUniforms uniforms{{
        enabled && ready_ ? 1u : 0u,
        Renderer::Lighting::ForwardPlus::TileSize,
        grid_.width,
        Renderer::Lighting::ForwardPlus::MaximumLightsPerTile,
    }};
    SDL_PushGPUFragmentUniformData(command, 1u, &uniforms, sizeof(uniforms));
    return true;
}

void ForwardPlus::clear()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (indices_) SDL_ReleaseGPUBuffer(device, indices_);
        if (counts_) SDL_ReleaseGPUBuffer(device, counts_);
        if (pipeline_) SDL_ReleaseGPUComputePipeline(device, pipeline_);
    }
    indices_ = nullptr;
    counts_ = nullptr;
    pipeline_ = nullptr;
    grid_ = {};
    width_ = 0u;
    height_ = 0u;
    ready_ = false;
}

} // namespace Renderer::Lighting::SDLGPU
