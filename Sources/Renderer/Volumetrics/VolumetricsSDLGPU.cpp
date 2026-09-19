#include "Renderer/Internal/VolumetricsSDLGPU.hpp"

#include "Renderer/Internal/AccelerationSDLGPU.hpp"
#include "Renderer/Internal/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/Internal/VolumetricMarchShader.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Acceleration.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Renderer::Internal {
namespace {

inline constexpr const char *BlurShader = R"HLSL(
Texture2D<float4> Input : register(t0, space0);
SamplerState InputSampler : register(s0, space0);
RWTexture2D<float4> Output : register(u0, space1);
cbuffer BlurData : register(b0, space2) {
    float Width;
    float Height;
    float Offset;
    float DepthFalloff;
};

float Weight(float center_depth, float sample_depth)
{
    float scale = max(center_depth, 1.0);
    return exp(-abs(sample_depth - center_depth) * max(DepthFalloff, 0.0) / scale);
}

[numthreads(8, 8, 1)]
void Main(uint3 tid : SV_DispatchThreadID)
{
    uint width = (uint)Width;
    uint height = (uint)Height;
    if (tid.x >= width || tid.y >= height) return;
    float2 size = max(float2(Width, Height), 1.0.xx);
    float2 uv = (float2(tid.xy) + 0.5.xx) / size;
    float2 delta = Offset / size;
    float4 center = Input.SampleLevel(InputSampler, uv, 0);
    float4 sum = center;
    float total = 1.0;
    float2 offsets[4] = {
        float2(-delta.x, -delta.y),
        float2( delta.x, -delta.y),
        float2(-delta.x,  delta.y),
        float2( delta.x,  delta.y)
    };
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        float4 value = Input.SampleLevel(InputSampler, saturate(uv + offsets[i]), 0);
        float weight = Weight(center.a, value.a);
        sum += value * weight;
        total += weight;
    }
    Output[tid.xy] = sum / max(total, 1.0e-5);
}
)HLSL";

inline constexpr const char *CompositeShader = R"HLSL(
Texture2D<float4> Volume : register(t0, space2);
SamplerState VolumeSampler : register(s0, space2);
Texture2D<float> SceneDepth : register(t1, space2);
SamplerState DepthSampler : register(s1, space2);

cbuffer FrameData : register(b0, space3) {
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
cbuffer VolumetricData : register(b1, space3) {
    float Density;
    float Anisotropy;
    float MaximumDistance;
    float DepthMode;
    uint SampleCount;
    uint FrameIndex;
    float Jitter;
    float DepthFalloff;
};

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 p = id == 0u ? float2(-1.0, -1.0) :
        (id == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    VSOut output;
    output.position = float4(p, 0.0, 1.0);
    output.uv = float2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    return output;
}

float3 CameraDirection(float2 uv)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    if (ProjectionAlpha.z > 0.5) return normalize(CameraForwardFar.xyz);
    return normalize(
        CameraForwardFar.xyz +
        CameraRightAspect.xyz * ndc.x * CameraUpTanHalfFov.w * CameraRightAspect.w +
        CameraUpTanHalfFov.xyz * ndc.y * CameraUpTanHalfFov.w);
}

float RayLength(float2 uv)
{
    float depth = SceneDepth.SampleLevel(DepthSampler, uv, 0).r;
    if (DepthMode > 1.5) {
        if (depth <= 0.0) return MaximumDistance;
        return min(depth, MaximumDistance);
    }
    if (depth >= 0.999999) return MaximumDistance;
    float near_plane = max(CameraPositionNear.w, 1.0e-5);
    float far_plane = CameraForwardFar.w;
    if (ProjectionAlpha.z > 0.5)
        return min(near_plane + depth * max(far_plane - near_plane, 1.0e-5), MaximumDistance);
    float forward_depth = far_plane < 3.0e37
        ? near_plane * far_plane /
            max(far_plane - depth * (far_plane - near_plane), 1.0e-5)
        : near_plane / max(1.0 - depth, 1.0e-5);
    float ray_cosine = max(dot(CameraDirection(uv), normalize(CameraForwardFar.xyz)), 1.0e-4);
    return min(forward_depth / ray_cosine, MaximumDistance);
}

float DepthWeight(float target_depth, float sample_depth)
{
    float scale = max(target_depth, 1.0);
    return exp(-abs(sample_depth - target_depth) * max(DepthFalloff, 0.0) / scale);
}

float4 PSMain(VSOut input) : SV_Target0
{
    float2 volume_size = max(Resolution.xy, 1.0.xx);
    float2 position = input.uv * volume_size - 0.5.xx;
    float2 base = floor(position);
    float2 fraction = position - base;
    float target_depth = RayLength(input.uv);
    float3 color = 0.0.xxx;
    float total = 0.0;

    [unroll] for (uint y = 0u; y < 2u; ++y) {
        [unroll] for (uint x = 0u; x < 2u; ++x) {
            float2 texel = base + float2(x, y) + 0.5.xx;
            float2 uv = saturate(texel / volume_size);
            float4 value = Volume.SampleLevel(VolumeSampler, uv, 0);
            float bilinear = (x == 0u ? 1.0 - fraction.x : fraction.x) *
                (y == 0u ? 1.0 - fraction.y : fraction.y);
            float weight = bilinear * DepthWeight(target_depth, value.a);
            color += value.rgb * weight;
            total += weight;
        }
    }

    color /= max(total, 1.0e-5);
    return float4(max(color, 0.0.xxx), 0.0);
}
)HLSL";

struct alignas(16) VolumetricUniforms {
    float density = 0.0f;
    float anisotropy = 0.0f;
    float maximum_distance = 0.0f;
    float depth_mode = 0.0f;
    std::uint32_t sample_count = 0u;
    std::uint32_t frame_index = 0u;
    float jitter = 0.0f;
    float depth_falloff = 0.0f;
};

struct alignas(16) BlurUniforms {
    float width = 1.0f;
    float height = 1.0f;
    float offset = 1.0f;
    float depth_falloff = 0.0f;
};

struct alignas(16) AccelerationUniforms {
    std::array<std::uint32_t, 4> counts0{};
    std::array<std::uint32_t, 4> counts1{};
};

struct State {
    SDL_GPUComputePipeline *march_pipeline = nullptr;
    SDL_GPUComputePipeline *blur_pipeline = nullptr;
    SDL_GPUGraphicsPipeline *composite_pipeline = nullptr;
    SDL_GPUSampler *linear_sampler = nullptr;
    SDL_GPUSampler *nearest_sampler = nullptr;
    SDL_GPUTexture *volume = nullptr;
    SDL_GPUTexture *ping = nullptr;
    SDL_GPUTexture *pong = nullptr;
    int width = 0;
    int height = 0;
    int volume_width = 0;
    int volume_height = 0;
    int resolution_divisor = 0;
    std::uint32_t frame_index = 0u;
    bool pipelines_attempted = false;
    Scenes::AccelerationScene acceleration;
    Scenes::SceneCache dynamic_scene;
    Scenes::SDLGPU::AccelerationResources acceleration_gpu;
    std::vector<Scenes::Scene::RenderItem> render_items;
    std::vector<Scenes::Scene::RenderItem> dynamic_items;
};

State state;

std::uint32_t gpuCount(std::size_t value)
{
    return static_cast<std::uint32_t>(std::min<std::size_t>(value, UINT32_MAX));
}

SDL_GPUGraphicsPipeline *createCompositePipeline(
    SDL_GPUShader *vertex,
    SDL_GPUShader *fragment)
{
    SDL_GPUColorTargetDescription color{};
    color.format = Renderer::SDLGPU::colorFormat();
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.enable_blend = true;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1u;
    return SDL_CreateGPUGraphicsPipeline(Renderer::SDLGPU::device(), &info);
}

bool ensurePipelines()
{
    if (state.march_pipeline && state.blur_pipeline && state.composite_pipeline &&
        state.linear_sampler && state.nearest_sampler)
        return true;
    if (!Renderer::SDLGPU::device()) return false;
    if (state.pipelines_attempted) return false;
    state.pipelines_attempted = true;

    state.march_pipeline = Renderer::SDLGPU::compileComputePipeline(
        VolumetricMarchShader,
        "Horse Volumetric March",
        "Main"
    );
    state.blur_pipeline = Renderer::SDLGPU::compileComputePipeline(
        BlurShader,
        "Horse Volumetric Kawase",
        "Main"
    );

    SDL_GPUShader *vertex = Renderer::SDLGPU::compileGraphicsShader(
        CompositeShader,
        SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
        "Horse Volumetric Composite VS",
        "VSMain"
    );
    SDL_GPUShader *fragment = Renderer::SDLGPU::compileGraphicsShader(
        CompositeShader,
        SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
        "Horse Volumetric Composite PS",
        "PSMain"
    );
    if (vertex && fragment)
        state.composite_pipeline = createCompositePipeline(vertex, fragment);
    if (fragment) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), fragment);
    if (vertex) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), vertex);

    state.linear_sampler = Renderer::SDLGPU::createLinearSampler();
    state.nearest_sampler = Renderer::SDLGPU::createNearestSampler();
    if (state.march_pipeline && state.blur_pipeline && state.composite_pipeline &&
        state.linear_sampler && state.nearest_sampler)
        return true;

    std::fprintf(
        stderr,
        "[Volumetrics/SDL_GPU]: pipeline initialization failed: %s\n",
        SDL_GetError()
    );
    return false;
}

void destroyTargets()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (state.pong) SDL_ReleaseGPUTexture(device, state.pong);
        if (state.ping) SDL_ReleaseGPUTexture(device, state.ping);
        if (state.volume) SDL_ReleaseGPUTexture(device, state.volume);
    }
    state.pong = nullptr;
    state.ping = nullptr;
    state.volume = nullptr;
    state.volume_width = 0;
    state.volume_height = 0;
}

bool ensureTargets(int width, int height, int divisor)
{
    const int volume_width = std::max((width + divisor - 1) / divisor, 1);
    const int volume_height = std::max((height + divisor - 1) / divisor, 1);
    if (state.volume && state.width == width && state.height == height &&
        state.volume_width == volume_width && state.volume_height == volume_height &&
        state.resolution_divisor == divisor)
        return true;

    destroyTargets();
    state.width = width;
    state.height = height;
    state.volume_width = volume_width;
    state.volume_height = volume_height;
    state.resolution_divisor = divisor;

    constexpr SDL_GPUTextureUsageFlags usage =
        SDL_GPU_TEXTUREUSAGE_SAMPLER |
        SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    const auto w = static_cast<std::uint32_t>(volume_width);
    const auto h = static_cast<std::uint32_t>(volume_height);
    state.volume = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(),
        usage,
        w,
        h,
        "Horse Volumetrics"
    );
    state.ping = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(),
        usage,
        w,
        h,
        "Horse Volumetrics Ping"
    );
    state.pong = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(),
        usage,
        w,
        h,
        "Horse Volumetrics Pong"
    );
    if (state.volume && state.ping && state.pong) return true;

    std::fprintf(
        stderr,
        "[Volumetrics/SDL_GPU]: target creation failed: %s\n",
        SDL_GetError()
    );
    destroyTargets();
    return false;
}

bool syncAcceleration(const Ecs::World& world, std::string *error)
{
    Scenes::Scene::collectRenderItems(world, state.render_items);
    if (!state.acceleration.sync(world, state.render_items, error)) return false;

    state.dynamic_items.clear();
    for (const Scenes::Scene::RenderItem& item : state.render_items) {
        if (item.layer != RenderLayer::World) continue;
        if (!Scenes::AccelerationScene::eligible(world, item))
            state.dynamic_items.push_back(item);
    }

    if (!state.dynamic_scene.syncGeometry(world, state.dynamic_items, error)) return false;
    return state.acceleration_gpu.sync(state.acceleration, state.dynamic_scene, error);
}

AccelerationUniforms accelerationUniforms()
{
    AccelerationUniforms result;
    result.counts0 = {
        gpuCount(state.acceleration_gpu.tlasNodeCount()),
        gpuCount(state.acceleration_gpu.instanceCount()),
        gpuCount(state.acceleration_gpu.blasNodeCount()),
        gpuCount(state.acceleration_gpu.blasCount()),
    };
    result.counts1 = {
        gpuCount(state.acceleration_gpu.localTriangleCount()),
        gpuCount(state.acceleration_gpu.dynamicNodeCount()),
        gpuCount(state.acceleration_gpu.dynamicTriangleCount()),
        0u,
    };
    return result;
}

bool march(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *depth,
    const SDLGPU::FrameUniforms& frame,
    const VolumetricUniforms& uniforms,
    const AccelerationUniforms& acceleration_uniforms,
    const GlobalIllumination::Field *global_illumination)
{
    SDL_PushGPUComputeUniformData(command, 0u, &frame, sizeof frame);
    SDL_PushGPUComputeUniformData(command, 1u, &uniforms, sizeof uniforms);
    SDL_PushGPUComputeUniformData(
        command,
        2u,
        &acceleration_uniforms,
        sizeof acceleration_uniforms
    );

    SDL_GPUStorageTextureReadWriteBinding writable{};
    writable.texture = state.volume;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(command, &writable, 1u, nullptr, 0u);
    if (!pass) return false;
    SDL_BindGPUComputePipeline(pass, state.march_pipeline);

    state.acceleration_gpu.bindCompute(pass, 0u);
    const bool shading_ok = bindGlobalIlluminationSDLGPU(pass, global_illumination, 7u);
    const SDL_GPUTextureSamplerBinding depth_binding{depth, state.nearest_sampler};
    SDL_BindGPUComputeSamplers(pass, 0u, &depth_binding, 1u);
    if (shading_ok) {
        SDL_DispatchGPUCompute(
            pass,
            (static_cast<Uint32>(state.volume_width) + 7u) / 8u,
            (static_cast<Uint32>(state.volume_height) + 7u) / 8u,
            1u
        );
    }
    SDL_EndGPUComputePass(pass);
    return shading_ok;
}

SDL_GPUTexture *blur(
    SDL_GPUCommandBuffer *command,
    std::uint32_t passes,
    float depth_falloff)
{
    SDL_GPUTexture *source = state.volume;
    for (std::uint32_t index = 0u; index < passes; ++index) {
        SDL_GPUTexture *target = (index & 1u) == 0u ? state.ping : state.pong;
        const BlurUniforms uniforms{
            static_cast<float>(state.volume_width),
            static_cast<float>(state.volume_height),
            1.0f + static_cast<float>(index),
            depth_falloff,
        };
        SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);
        SDL_GPUStorageTextureReadWriteBinding writable{};
        writable.texture = target;
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(command, &writable, 1u, nullptr, 0u);
        if (!pass) return nullptr;
        SDL_BindGPUComputePipeline(pass, state.blur_pipeline);
        const SDL_GPUTextureSamplerBinding input{source, state.linear_sampler};
        SDL_BindGPUComputeSamplers(pass, 0u, &input, 1u);
        SDL_DispatchGPUCompute(
            pass,
            (static_cast<Uint32>(state.volume_width) + 7u) / 8u,
            (static_cast<Uint32>(state.volume_height) + 7u) / 8u,
            1u
        );
        SDL_EndGPUComputePass(pass);
        source = target;
    }
    return source;
}

bool composite(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *color,
    SDL_GPUTexture *depth,
    SDL_GPUTexture *volume,
    const SDLGPU::FrameUniforms& frame,
    const VolumetricUniforms& uniforms)
{
    SDL_PushGPUFragmentUniformData(command, 0u, &frame, sizeof frame);
    SDL_PushGPUFragmentUniformData(command, 1u, &uniforms, sizeof uniforms);

    SDL_GPUColorTargetInfo target{};
    target.texture = color;
    target.load_op = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &target, 1u, nullptr);
    if (!pass) return false;
    SDL_BindGPUGraphicsPipeline(pass, state.composite_pipeline);
    const SDL_GPUTextureSamplerBinding samplers[2] = {
        {volume, state.nearest_sampler},
        {depth, state.nearest_sampler},
    };
    SDL_BindGPUFragmentSamplers(pass, 0u, samplers, 2u);
    SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);
    SDL_EndGPURenderPass(pass);
    return true;
}

} // namespace

bool renderVolumetricsSDLGPU(
    const Ecs::World& world,
    FrameOutput& output,
    const Volumetrics::Settings& settings)
{
    if (!ensurePipelines()) return false;
    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    auto *color = static_cast<SDL_GPUTexture *>(output.color_texture);
    auto *depth = static_cast<SDL_GPUTexture *>(output.depth_texture);
    if (!command || !color || !depth) return false;

    std::string error;
    if (!syncAcceleration(world, &error)) {
        std::fprintf(
            stderr,
            "[Volumetrics/SDL_GPU]: acceleration sync failed: %s\n",
            error.c_str()
        );
        return false;
    }

    const int width = std::max(output.width, 1);
    const int height = std::max(output.height, 1);
    if (!ensureTargets(width, height, settings.resolution_divisor)) return false;

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    if (!camera.valid) return true;

    const std::size_t node_count =
        state.acceleration_gpu.tlasNodeCount() +
        state.acceleration_gpu.blasNodeCount() +
        state.acceleration_gpu.dynamicNodeCount();
    const std::size_t triangle_count =
        state.acceleration_gpu.localTriangleCount() +
        state.acceleration_gpu.dynamicTriangleCount();
    const SDLGPU::FrameUniforms frame = SDLGPU::makeFrameUniforms(
        camera,
        state.volume_width,
        state.volume_height,
        width,
        height,
        node_count,
        triangle_count,
        0u,
        0u,
        Scenes::SceneCache::opacityCutoff(),
        state.frame_index
    );
    const VolumetricUniforms uniforms{
        settings.density,
        settings.anisotropy,
        settings.maximum_distance,
        output.depth == DepthSource::LinearTexture ? 2.0f : 1.0f,
        settings.sample_count,
        state.frame_index,
        settings.jitter,
        settings.depth_falloff,
    };
    const AccelerationUniforms acceleration_uniforms = accelerationUniforms();

    if (!march(
            command,
            depth,
            frame,
            uniforms,
            acceleration_uniforms,
            output.global_illumination))
        return false;

    SDL_GPUTexture *filtered = blur(command, settings.blur_passes, settings.depth_falloff);
    if (!filtered || !composite(command, color, depth, filtered, frame, uniforms)) return false;
    ++state.frame_index;
    return true;
}

void shutdownVolumetricsSDLGPU()
{
    destroyTargets();
    state.acceleration_gpu.clear();
    state.acceleration.clear();
    state.dynamic_scene.clear();
    state.render_items.clear();
    state.dynamic_items.clear();

    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (state.nearest_sampler) SDL_ReleaseGPUSampler(device, state.nearest_sampler);
        if (state.linear_sampler) SDL_ReleaseGPUSampler(device, state.linear_sampler);
        if (state.composite_pipeline) SDL_ReleaseGPUGraphicsPipeline(device, state.composite_pipeline);
        if (state.blur_pipeline) SDL_ReleaseGPUComputePipeline(device, state.blur_pipeline);
        if (state.march_pipeline) SDL_ReleaseGPUComputePipeline(device, state.march_pipeline);
    }
    state.nearest_sampler = nullptr;
    state.linear_sampler = nullptr;
    state.composite_pipeline = nullptr;
    state.blur_pipeline = nullptr;
    state.march_pipeline = nullptr;
    state.width = 0;
    state.height = 0;
    state.resolution_divisor = 0;
    state.frame_index = 0u;
    state.pipelines_attempted = false;
}

} // namespace Renderer::Internal
