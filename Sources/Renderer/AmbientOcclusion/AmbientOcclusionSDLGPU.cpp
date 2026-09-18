#include "Renderer/Internal/AmbientOcclusionSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace Renderer::AmbientOcclusion::SDLGPU {
namespace {

inline constexpr const char *OcclusionShader = R"HLSL(
Texture2D<float> Depth : register(t0, space0);
SamplerState DepthSampler : register(s0, space0);
Texture2D<float4> Normal : register(t1, space0);
SamplerState NormalSampler : register(s1, space0);
RWTexture2D<float4> Output : register(u0, space1);

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

cbuffer OcclusionData : register(b1, space2) {
    uint4 Info;
    float4 Params;
};

float ViewDepth(float depth)
{
    float near_plane = max(CameraPositionNear.w, 1.0e-5);
    float far_plane = CameraForwardFar.w;
    if (ProjectionAlpha.z > 0.5)
        return lerp(near_plane, far_plane, saturate(depth));
    if (far_plane < 3.0e37)
        return near_plane * far_plane /
            max(far_plane - depth * (far_plane - near_plane), 1.0e-5);
    return near_plane / max(1.0 - depth, 1.0e-5);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    float view_depth = ViewDepth(depth);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    if (ProjectionAlpha.z > 0.5) {
        return CameraPositionNear.xyz +
            CameraForwardFar.xyz * view_depth +
            CameraRightAspect.xyz * (ndc.x * ProjectionAlpha.x) +
            CameraUpTanHalfFov.xyz * (ndc.y * ProjectionAlpha.y);
    }
    return CameraPositionNear.xyz +
        CameraForwardFar.xyz * view_depth +
        CameraRightAspect.xyz *
            (ndc.x * CameraUpTanHalfFov.w * CameraRightAspect.w * view_depth) +
        CameraUpTanHalfFov.xyz *
            (ndc.y * CameraUpTanHalfFov.w * view_depth);
}

float RandomAngle(uint2 pixel)
{
    float value = dot(float2(pixel), float2(0.06711056, 0.00583715));
    return frac(52.9829189 * frac(value)) * 6.28318530718;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 size = max(Info.xy, uint2(1u, 1u));
    if (any(id.xy >= size)) return;

    float2 size_f = float2((float)size.x, (float)size.y);
    float2 uv = (float2(id.xy) + 0.5.xx) / size_f;
    float depth = Depth.SampleLevel(DepthSampler, uv, 0.0).r;
    float3 normal = Normal.SampleLevel(NormalSampler, uv, 0.0).xyz;
    float normal_length = length(normal);
    if (depth >= 0.999999 || normal_length < 0.25) {
        Output[id.xy] = 1.0.xxxx;
        return;
    }
    normal /= normal_length;

    float radius = max(Params.x, 1.0e-4);
    float strength = max(Params.y, 0.0);
    uint sample_count = max(Info.z, 1u);
    float view_depth = ViewDepth(depth);
    float pixels_per_world = ProjectionAlpha.z > 0.5
        ? (float)size.y / max(2.0 * ProjectionAlpha.y, 1.0e-5)
        : (float)size.y /
            max(2.0 * CameraUpTanHalfFov.w * view_depth, 1.0e-5);
    float radius_pixels = clamp(radius * pixels_per_world, 1.0, 192.0);
    float3 position = ReconstructWorld(uv, depth);
    float rotation = RandomAngle(id.xy);

    float occlusion = 0.0;
    float weight_sum = 0.0;
    const float golden_angle = 2.39996322973;
    for (uint index = 0u; index < sample_count; ++index) {
        float fraction = ((float)index + 0.5) / (float)sample_count;
        float distance_fraction = sqrt(fraction);
        float angle = rotation + (float)index * golden_angle;
        float2 direction = float2(cos(angle), sin(angle));
        float2 sample_pixel = float2(id.xy) + 0.5.xx +
            direction * radius_pixels * distance_fraction;
        float2 sample_uv = sample_pixel / size_f;
        if (any(sample_uv <= 0.0.xx) || any(sample_uv >= 1.0.xx)) continue;

        float sample_depth = Depth.SampleLevel(DepthSampler, sample_uv, 0.0).r;
        if (sample_depth >= 0.999999) continue;
        float3 sample_position = ReconstructWorld(sample_uv, sample_depth);
        float3 delta = sample_position - position;
        float distance_to_sample = length(delta);
        if (distance_to_sample <= 1.0e-5 || distance_to_sample >= radius) continue;

        float horizon = dot(normal, delta / distance_to_sample);
        float range_weight = saturate(1.0 - distance_to_sample / radius);
        float sample_occlusion = saturate((horizon - 0.035) * 2.5) * range_weight;
        occlusion += sample_occlusion;
        weight_sum += range_weight;
    }

    float normalized = weight_sum > 1.0e-5 ? occlusion / weight_sum : 0.0;
    float visibility = saturate(1.0 - normalized * strength);
    Output[id.xy] = visibility.xxxx;
}
)HLSL";

inline constexpr const char *FilterShader = R"HLSL(
Texture2D<float4> Occlusion : register(t0, space0);
SamplerState OcclusionSampler : register(s0, space0);
Texture2D<float> Depth : register(t1, space0);
SamplerState DepthSampler : register(s1, space0);
Texture2D<float4> Normal : register(t2, space0);
SamplerState NormalSampler : register(s2, space0);
RWTexture2D<float4> Output : register(u0, space1);

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

cbuffer OcclusionData : register(b1, space2) {
    uint4 Info;
    float4 Params;
};

float ViewDepth(float depth)
{
    float near_plane = max(CameraPositionNear.w, 1.0e-5);
    float far_plane = CameraForwardFar.w;
    if (ProjectionAlpha.z > 0.5)
        return lerp(near_plane, far_plane, saturate(depth));
    if (far_plane < 3.0e37)
        return near_plane * far_plane /
            max(far_plane - depth * (far_plane - near_plane), 1.0e-5);
    return near_plane / max(1.0 - depth, 1.0e-5);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 size = max(Info.xy, uint2(1u, 1u));
    if (any(id.xy >= size)) return;

    float2 size_f = float2((float)size.x, (float)size.y);
    float2 texel = 1.0.xx / size_f;
    float2 uv = (float2(id.xy) + 0.5.xx) / size_f;
    float center_depth_sample = Depth.SampleLevel(DepthSampler, uv, 0.0).r;
    float3 center_normal = Normal.SampleLevel(NormalSampler, uv, 0.0).xyz;
    float normal_length = length(center_normal);
    if (center_depth_sample >= 0.999999 || normal_length < 0.25) {
        Output[id.xy] = 1.0.xxxx;
        return;
    }
    center_normal /= normal_length;
    float center_depth = ViewDepth(center_depth_sample);

    float total = 0.0;
    float weight_sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 sample_uv = saturate(uv + float2((float)x, (float)y) * texel);
            float sample_depth_value = Depth.SampleLevel(DepthSampler, sample_uv, 0.0).r;
            float3 sample_normal = Normal.SampleLevel(NormalSampler, sample_uv, 0.0).xyz;
            float sample_length = length(sample_normal);
            if (sample_depth_value >= 0.999999 || sample_length < 0.25) continue;
            sample_normal /= sample_length;

            float sample_depth = ViewDepth(sample_depth_value);
            float depth_scale = max(Params.x * 0.25, 0.05);
            float depth_weight = exp(-abs(sample_depth - center_depth) / depth_scale);
            float normal_weight = pow(saturate(dot(center_normal, sample_normal)), 8.0);
            float spatial_weight = (x == 0 && y == 0) ? 1.0 :
                ((x == 0 || y == 0) ? 0.75 : 0.5);
            float weight = depth_weight * normal_weight * spatial_weight;
            total += Occlusion.SampleLevel(OcclusionSampler, sample_uv, 0.0).r * weight;
            weight_sum += weight;
        }
    }

    float visibility = weight_sum > 1.0e-5
        ? total / weight_sum
        : Occlusion.SampleLevel(OcclusionSampler, uv, 0.0).r;
    Output[id.xy] = saturate(visibility).xxxx;
}
)HLSL";

struct alignas(16) OcclusionUniforms {
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t sample_count = 0u;
    std::uint32_t reserved = 0u;
    float radius = 1.0f;
    float strength = 1.0f;
    float reserved0 = 0.0f;
    float reserved1 = 0.0f;
};

struct alignas(16) FragmentUniforms {
    std::uint32_t enabled = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t reserved = 0u;
};

static_assert(sizeof(OcclusionUniforms) == 32u);
static_assert(sizeof(FragmentUniforms) == 16u);

constexpr SDL_GPUTextureUsageFlags OcclusionUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

bool fail(std::string *error, const char *message)
{
    if (error) *error = message;
    return false;
}

} // namespace

Pass::~Pass()
{
    clear();
}

bool Pass::init()
{
    if (pipeline_ && filter_pipeline_ && sampler_) return true;
    if (!Renderer::SDLGPU::device()) return false;

    if (!pipeline_)
        pipeline_ = Renderer::SDLGPU::compileComputePipeline(
            OcclusionShader,
            "Horse Ambient Occlusion",
            "main"
        );
    if (!filter_pipeline_)
        filter_pipeline_ = Renderer::SDLGPU::compileComputePipeline(
            FilterShader,
            "Horse Ambient Occlusion Filter",
            "main"
        );
    if (!sampler_) sampler_ = Renderer::SDLGPU::createNearestSampler();

    if (!pipeline_ || !filter_pipeline_ || !sampler_) {
        clear();
        return false;
    }
    return true;
}

void Pass::clearTextures()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (filtered_) SDL_ReleaseGPUTexture(device, filtered_);
        if (raw_) SDL_ReleaseGPUTexture(device, raw_);
    }
    filtered_ = nullptr;
    raw_ = nullptr;
    width_ = 0u;
    height_ = 0u;
    ready_ = false;
}

bool Pass::resize(std::uint32_t width, std::uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (!init()) return false;
    if (raw_ && filtered_ && width_ == width && height_ == height) return true;

    clearTextures();
    raw_ = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        OcclusionUsage,
        width,
        height,
        "Horse Ambient Occlusion Raw"
    );
    filtered_ = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        OcclusionUsage,
        width,
        height,
        "Horse Ambient Occlusion Filtered"
    );
    if (!raw_ || !filtered_) {
        clearTextures();
        return false;
    }

    width_ = width;
    height_ = height;
    ready_ = true;
    return true;
}

bool Pass::build(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *depth,
    SDL_GPUTexture *normal,
    const Renderer::SDLGPU::FrameUniforms& frame,
    const Settings& settings,
    std::string *error)
{
    if (error) error->clear();
    if (!command || !depth || !normal || !ready_ || !pipeline_ ||
        !filter_pipeline_ || !sampler_)
        return fail(error, "ambient occlusion resources are not ready");

    const OcclusionUniforms uniforms{
        width_,
        height_,
        std::clamp(settings.sample_count, 1u, 12u),
        0u,
        std::max(settings.radius, 1.0e-4f),
        std::max(settings.strength, 0.0f),
        0.0f,
        0.0f,
    };

    SDL_PushGPUComputeUniformData(command, 0u, &frame, sizeof(frame));
    SDL_PushGPUComputeUniformData(command, 1u, &uniforms, sizeof(uniforms));

    SDL_GPUStorageTextureReadWriteBinding raw_binding{};
    raw_binding.texture = raw_;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
        command,
        &raw_binding,
        1u,
        nullptr,
        0u
    );
    if (!pass) return fail(error, "failed to begin ambient occlusion compute pass");

    SDL_BindGPUComputePipeline(pass, pipeline_);
    const std::array<SDL_GPUTextureSamplerBinding, 2> inputs {{
        {depth, sampler_},
        {normal, sampler_},
    }};
    SDL_BindGPUComputeSamplers(
        pass,
        0u,
        inputs.data(),
        static_cast<Uint32>(inputs.size())
    );
    SDL_DispatchGPUCompute(pass, (width_ + 7u) / 8u, (height_ + 7u) / 8u, 1u);
    SDL_EndGPUComputePass(pass);

    SDL_PushGPUComputeUniformData(command, 0u, &frame, sizeof(frame));
    SDL_PushGPUComputeUniformData(command, 1u, &uniforms, sizeof(uniforms));

    SDL_GPUStorageTextureReadWriteBinding filtered_binding{};
    filtered_binding.texture = filtered_;
    pass = SDL_BeginGPUComputePass(
        command,
        &filtered_binding,
        1u,
        nullptr,
        0u
    );
    if (!pass) return fail(error, "failed to begin ambient occlusion filter pass");

    SDL_BindGPUComputePipeline(pass, filter_pipeline_);
    const std::array<SDL_GPUTextureSamplerBinding, 3> filter_inputs {{
        {raw_, sampler_},
        {depth, sampler_},
        {normal, sampler_},
    }};
    SDL_BindGPUComputeSamplers(
        pass,
        0u,
        filter_inputs.data(),
        static_cast<Uint32>(filter_inputs.size())
    );
    SDL_DispatchGPUCompute(pass, (width_ + 7u) / 8u, (height_ + 7u) / 8u, 1u);
    SDL_EndGPUComputePass(pass);
    return true;
}

bool Pass::bind(
    SDL_GPURenderPass *pass,
    SDL_GPUCommandBuffer *command,
    bool enabled,
    std::uint32_t sampler_slot) const
{
    if (!pass || !command || !filtered_ || !sampler_) return false;
    const SDL_GPUTextureSamplerBinding binding{filtered_, sampler_};
    SDL_BindGPUFragmentSamplers(pass, sampler_slot, &binding, 1u);
    const FragmentUniforms uniforms{
        enabled ? 1u : 0u,
        width_,
        height_,
        0u,
    };
    SDL_PushGPUFragmentUniformData(command, 2u, &uniforms, sizeof(uniforms));
    return true;
}

void Pass::clear()
{
    clearTextures();
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (sampler_) SDL_ReleaseGPUSampler(device, sampler_);
        if (filter_pipeline_) SDL_ReleaseGPUComputePipeline(device, filter_pipeline_);
        if (pipeline_) SDL_ReleaseGPUComputePipeline(device, pipeline_);
    }
    sampler_ = nullptr;
    filter_pipeline_ = nullptr;
    pipeline_ = nullptr;
}

} // namespace Renderer::AmbientOcclusion::SDLGPU
