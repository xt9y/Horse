#include "Renderer/Volumetrics/VolumetricsSDLGPU.hpp"

#include "Renderer/GlobalIllumination/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneResourcesSDLGPU.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Renderer::Internal {
namespace {

inline constexpr const char *MarchShader = R"HLSL(
struct GpuNode {
    float3 minimum; uint first;
    float3 maximum; uint meta;
    uint4 extra;
};
struct GpuTriangle {
    float4 p0; float4 p1; float4 p2;
    float4 n0; float4 n1; float4 n2;
    float4 uv01; float4 uv2;
};

StructuredBuffer<GpuNode> Nodes : register(t0, space0);
StructuredBuffer<GpuTriangle> Triangles : register(t1, space0);
StructuredBuffer<float4> Shading : register(t2, space0);
RWTexture2D<float4> Output : register(u0, space1);
Texture2D<float> SceneDepth : register(t0, space2);
SamplerState DepthSampler : register(s0, space2);

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
    float Padding;
};

uint Hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float Random(uint state)
{
    return (Hash(state) & 0x00ffffffu) / 16777216.0;
}

void CameraRay(float2 pixel, out float3 origin, out float3 direction)
{
    float2 ndc = ((pixel + 0.5.xx) / max(Resolution.xy, 1.0.xx)) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    if (ProjectionAlpha.z > 0.5) {
        origin = CameraPositionNear.xyz +
            CameraRightAspect.xyz * ndc.x * ProjectionAlpha.x +
            CameraUpTanHalfFov.xyz * ndc.y * ProjectionAlpha.y;
        direction = normalize(CameraForwardFar.xyz);
    } else {
        origin = CameraPositionNear.xyz;
        direction = normalize(
            CameraForwardFar.xyz +
            CameraRightAspect.xyz * ndc.x * CameraUpTanHalfFov.w * CameraRightAspect.w +
            CameraUpTanHalfFov.xyz * ndc.y * CameraUpTanHalfFov.w);
    }
}

float RayLength(float2 uv, float3 direction)
{
    float depth = SceneDepth.SampleLevel(DepthSampler, uv, 0).r;
    if (DepthMode > 1.5) {
        if (depth <= 0.0) return MaximumDistance;
        return min(depth, MaximumDistance);
    }

    if (depth >= 0.999999) return MaximumDistance;
    float near_plane = max(CameraPositionNear.w, 1.0e-5);
    float far_plane = CameraForwardFar.w;
    float forward_depth;
    if (ProjectionAlpha.z > 0.5) {
        forward_depth = near_plane + depth * max(far_plane - near_plane, 1.0e-5);
        return min(max(forward_depth, 0.0), MaximumDistance);
    }
    if (far_plane < 3.0e37) {
        forward_depth = near_plane * far_plane /
            max(far_plane - depth * (far_plane - near_plane), 1.0e-5);
    } else {
        forward_depth = near_plane / max(1.0 - depth, 1.0e-5);
    }
    float ray_cosine = max(dot(direction, normalize(CameraForwardFar.xyz)), 1.0e-4);
    return min(max(forward_depth / ray_cosine, 0.0), MaximumDistance);
}

bool IntersectAabb(float3 ro, float3 inv_rd, float3 bmin, float3 bmax, float max_t)
{
    float3 t0 = (bmin - ro) * inv_rd;
    float3 t1 = (bmax - ro) * inv_rd;
    float3 mn = min(t0, t1);
    float3 mx = max(t0, t1);
    float enter = max(max(mn.x, mn.y), max(mn.z, 0.0));
    float leave = min(min(mx.x, mx.y), mx.z);
    return leave >= enter && enter < max_t;
}

bool IntersectTriangle(float3 ro, float3 rd, GpuTriangle tri, float max_t)
{
    float3 e1 = tri.p1.xyz - tri.p0.xyz;
    float3 e2 = tri.p2.xyz - tri.p0.xyz;
    float3 p = cross(rd, e2);
    float det = dot(e1, p);
    if (abs(det) < 1.0e-7) return false;
    float inv_det = 1.0 / det;
    float3 s = ro - tri.p0.xyz;
    float u = dot(s, p) * inv_det;
    if (u < 0.0 || u > 1.0) return false;
    float3 q = cross(s, e1);
    float v = dot(rd, q) * inv_det;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(e2, q) * inv_det;
    return t > 1.0e-4 && t < max_t;
}

bool Occluded(float3 ro, float3 rd, float max_t)
{
    if (Counts.x <= 0 || Counts.y <= 0 || max_t <= 0.0) return false;
    float3 safe_rd = sign(rd + 1.0e-20.xxx) * max(abs(rd), 1.0e-8.xxx);
    float3 inv_rd = 1.0 / safe_rd;
    uint stack[64];
    uint top = 0u;
    stack[top++] = 0u;
    while (top > 0u) {
        uint index = stack[--top];
        if (index >= (uint)Counts.x) continue;
        GpuNode node = Nodes[index];
        if (!IntersectAabb(ro, inv_rd, node.minimum, node.maximum, max_t)) continue;
        if ((node.meta & 0x80000000u) != 0u) {
            uint count = node.meta & 0x7fffffffu;
            for (uint i = 0u; i < count; ++i) {
                uint triangle = node.first + i;
                if (triangle < (uint)Counts.y &&
                    IntersectTriangle(ro, rd, Triangles[triangle], max_t))
                    return true;
            }
        } else if (top + 2u <= 64u) {
            stack[top++] = node.first;
            stack[top++] = node.meta;
        }
    }
    return false;
}

float Phase(float cosine_theta)
{
    const float PI = 3.14159265359;
    float g = clamp(Anisotropy, -0.95, 0.95);
    float g2 = g * g;
    float denominator = max(1.0 + g2 - 2.0 * g * cosine_theta, 1.0e-4);
    return (1.0 - g2) / (4.0 * PI * pow(denominator, 1.5));
}

float3 LightScattering(uint index, float3 position, float3 ray_direction)
{
    uint light_base = (uint)max(Shading[11].w, 12.0);
    uint offset = light_base + index * 5u;
    float4 position_intensity = Shading[offset + 0u];
    float4 direction_type = Shading[offset + 1u];
    float4 color_range = Shading[offset + 2u];
    float4 cone_shadow = Shading[offset + 3u];
    float4 volumetric = Shading[offset + 4u];
    if (volumetric.y < 0.5 || volumetric.x <= 0.0 || position_intensity.w <= 0.0)
        return 0.0.xxx;

    float type = direction_type.w;
    float3 light_direction = normalize(direction_type.xyz);
    float3 to_light = -light_direction;
    float attenuation = 1.0;
    float max_t = 1.0e30;

    if (type < 1.5 || type > 2.5) {
        float3 delta = position_intensity.xyz - position;
        float distance_to_light = length(delta);
        if (distance_to_light <= 1.0e-5) return 0.0.xxx;
        to_light = delta / distance_to_light;
        max_t = max(distance_to_light - cone_shadow.w, 0.0);
        attenuation = 1.0 / max(distance_to_light * distance_to_light, 1.0);
        if (color_range.w > 0.0)
            attenuation *= saturate(1.0 - distance_to_light / color_range.w);
        if (type > 2.5) {
            float cone = dot(-to_light, light_direction);
            attenuation *= smoothstep(cone_shadow.y, cone_shadow.x, cone);
        }
    }

    if (attenuation <= 0.0) return 0.0.xxx;
    if (cone_shadow.z > 0.5) {
        float bias = max(cone_shadow.w, 1.0e-4);
        if (Occluded(position + to_light * bias, to_light, max_t)) return 0.0.xxx;
    }

    float cosine_theta = dot(to_light, ray_direction);
    return color_range.rgb * position_intensity.w * attenuation *
        volumetric.x * Phase(cosine_theta);
}

[numthreads(8, 8, 1)]
void Main(uint3 tid : SV_DispatchThreadID)
{
    uint width = (uint)Resolution.x;
    uint height = (uint)Resolution.y;
    if (tid.x >= width || tid.y >= height) return;

    float2 uv = (float2(tid.xy) + 0.5.xx) / max(Resolution.xy, 1.0.xx);
    float3 origin, ray_direction;
    CameraRay(float2(tid.xy), origin, ray_direction);
    float ray_length = RayLength(uv, ray_direction);
    uint samples = max(SampleCount, 1u);
    float step_length = ray_length / samples;
    float grain = Random(tid.x + tid.y * width + FrameIndex * 747796405u + 1u);
    float first = lerp(0.5, grain, saturate(Jitter));
    float3 scattering = 0.0.xxx;
    uint light_count = (uint)max(Shading[11].z, 0.0);

    for (uint sample = 0u; sample < samples; ++sample) {
        float distance_along_ray = min((sample + first) * step_length, ray_length);
        float3 position = origin + ray_direction * distance_along_ray;
        float3 light_scattering = 0.0.xxx;
        for (uint light = 0u; light < light_count; ++light)
            light_scattering += LightScattering(light, position, ray_direction);
        float transmission = exp(-max(Density, 0.0) * distance_along_ray);
        scattering += light_scattering * max(Density, 0.0) * step_length * transmission;
    }

    Output[tid.xy] = float4(max(scattering, 0.0.xxx), ray_length);
}
)HLSL";

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
    Scenes::SDLGPU::SceneResources fallback_scene;
};

State state;

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

    state.march_pipeline = Renderer::SDLGPU::compileComputePipeline(
        MarchShader, "Horse Volumetric March", "Main");
    state.blur_pipeline = Renderer::SDLGPU::compileComputePipeline(
        BlurShader, "Horse Volumetric Kawase", "Main");

    SDL_GPUShader *vertex = Renderer::SDLGPU::compileGraphicsShader(
        CompositeShader, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
        "Horse Volumetric Composite VS", "VSMain");
    SDL_GPUShader *fragment = Renderer::SDLGPU::compileGraphicsShader(
        CompositeShader, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
        "Horse Volumetric Composite PS", "PSMain");
    if (vertex && fragment)
        state.composite_pipeline = createCompositePipeline(vertex, fragment);
    if (fragment) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), fragment);
    if (vertex) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), vertex);

    state.linear_sampler = Renderer::SDLGPU::createLinearSampler();
    state.nearest_sampler = Renderer::SDLGPU::createNearestSampler();
    if (state.march_pipeline && state.blur_pipeline && state.composite_pipeline &&
        state.linear_sampler && state.nearest_sampler)
        return true;

    std::fprintf(stderr, "[Volumetrics/SDL_GPU]: pipeline initialization failed: %s\n", SDL_GetError());
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
        Renderer::SDLGPU::colorFormat(), usage, w, h, "Horse Volumetrics");
    state.ping = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(), usage, w, h, "Horse Volumetrics Ping");
    state.pong = Renderer::SDLGPU::createTexture(
        Renderer::SDLGPU::colorFormat(), usage, w, h, "Horse Volumetrics Pong");
    if (state.volume && state.ping && state.pong) return true;

    std::fprintf(stderr, "[Volumetrics/SDL_GPU]: target creation failed: %s\n", SDL_GetError());
    destroyTargets();
    return false;
}

bool march(
    SDL_GPUCommandBuffer *command,
    Scenes::SDLGPU::SceneResources& scene,
    SDL_GPUTexture *depth,
    const SDLGPU::FrameUniforms& frame,
    const VolumetricUniforms& uniforms,
    const GlobalIllumination::Field *global_illumination)
{
    SDL_PushGPUComputeUniformData(command, 0u, &frame, sizeof frame);
    SDL_PushGPUComputeUniformData(command, 1u, &uniforms, sizeof uniforms);

    SDL_GPUStorageTextureReadWriteBinding writable{};
    writable.texture = state.volume;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(command, &writable, 1u, nullptr, 0u);
    if (!pass) return false;
    SDL_BindGPUComputePipeline(pass, state.march_pipeline);

    SDL_GPUBuffer *visibility[] = {scene.nodeBuffer(), scene.triangleBuffer()};
    SDL_BindGPUComputeStorageBuffers(pass, 0u, visibility, 2u);
    const bool shading_ok = bindGlobalIlluminationSDLGPU(pass, global_illumination, 2u);
    const SDL_GPUTextureSamplerBinding depth_binding{depth, state.nearest_sampler};
    SDL_BindGPUComputeSamplers(pass, 0u, &depth_binding, 1u);
    if (shading_ok) {
        SDL_DispatchGPUCompute(
            pass,
            (static_cast<Uint32>(state.volume_width) + 7u) / 8u,
            (static_cast<Uint32>(state.volume_height) + 7u) / 8u,
            1u);
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
            1u);
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
    auto *scene = static_cast<Scenes::SDLGPU::SceneResources *>(output.scene_resources);
    if (!command || !color || !depth) return false;
    if (!scene) {
        std::string error;
        const auto sync = state.fallback_scene.sync(world, &error);
        if (!sync.ok) {
            std::fprintf(
                stderr,
                "[Volumetrics/SDL_GPU]: trace scene sync failed: %s\n",
                error.c_str());
            return false;
        }
        scene = &state.fallback_scene;
    }

    const int width = std::max(output.width, 1);
    const int height = std::max(output.height, 1);
    if (!ensureTargets(width, height, settings.resolution_divisor)) return false;

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    if (!camera.valid) return true;

    const SDLGPU::FrameUniforms frame = SDLGPU::makeFrameUniforms(
        camera,
        state.volume_width, state.volume_height,
        width, height,
        scene->nodeCount(), scene->triangleCount(),
        scene->materialCount(), scene->textureCount(),
        Scenes::SceneCache::opacityCutoff(),
        state.frame_index);
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

    if (!march(command, *scene, depth, frame, uniforms, output.global_illumination)) return false;
    SDL_GPUTexture *filtered = blur(command, settings.blur_passes, settings.depth_falloff);
    if (!filtered || !composite(command, color, depth, filtered, frame, uniforms)) return false;
    ++state.frame_index;
    return true;
}

void shutdownVolumetricsSDLGPU()
{
    destroyTargets();
    state.fallback_scene.clear();
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
}

} // namespace Renderer::Internal
