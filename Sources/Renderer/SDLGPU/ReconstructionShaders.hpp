#ifndef HORSE_RENDERER_SDLGPU_RECONSTRUCTION_SHADERS_HPP
#define HORSE_RENDERER_SDLGPU_RECONSTRUCTION_SHADERS_HPP

namespace Renderer::SDLGPU::ReconstructionShaders {

inline constexpr const char *Resolve = R"HLSL(
Texture2D<float4> FreshColor : register(t0, space0);
SamplerState FreshColorSampler : register(s0, space0);
Texture2D<float> FreshDepth : register(t1, space0);
SamplerState FreshDepthSampler : register(s1, space0);
Texture2D<float4> FreshSurface : register(t2, space0);
SamplerState FreshSurfaceSampler : register(s2, space0);
Texture2D<float4> PreviousColor : register(t3, space0);
SamplerState PreviousColorSampler : register(s3, space0);
Texture2D<float> PreviousDepth : register(t4, space0);
SamplerState PreviousDepthSampler : register(s4, space0);
Texture2D<float4> PreviousSurface : register(t5, space0);
SamplerState PreviousSurfaceSampler : register(s5, space0);
Texture2D<float> PreviousAge : register(t6, space0);
SamplerState PreviousAgeSampler : register(s6, space0);

RWTexture2D<float4> OutputColor : register(u0, space1);
RWTexture2D<float> OutputDepth : register(u1, space1);
RWTexture2D<float4> NextColor : register(u2, space1);
RWTexture2D<float> NextDepth : register(u3, space1);
RWTexture2D<float4> NextSurface : register(u4, space1);
RWTexture2D<float> NextAge : register(u5, space1);

cbuffer ReconstructionData : register(b0, space2) {
    float4 CurrentPositionNear;
    float4 CurrentForwardFar;
    float4 CurrentRightAspect;
    float4 CurrentUpTanHalfFov;
    float4 CurrentProjectionAlpha;
    float4 PreviousPositionNear;
    float4 PreviousForwardFar;
    float4 PreviousRightAspect;
    float4 PreviousUpTanHalfFov;
    float4 PreviousProjectionAlpha;
    uint4 ResolutionGridPhase;
    uint4 Control;
    float4 Params;
};

bool Scheduled(uint2 pixel)
{
    uint grid = max(ResolutionGridPhase.z, 1u);
    uint phase = ResolutionGridPhase.w % max(grid * grid, 1u);
    return (pixel.x % grid) + (pixel.y % grid) * grid == phase;
}

float2 PixelUv(uint2 pixel)
{
    return (float2(pixel) + 0.5.xx) /
        max(float2(ResolutionGridPhase.xy), 1.0.xx);
}

void CameraRay(
    float2 pixel,
    float4 position_near,
    float4 forward_far,
    float4 right_aspect,
    float4 up_tan_half_fov,
    float4 projection_alpha,
    out float3 origin,
    out float3 direction)
{
    float2 size = max(float2(ResolutionGridPhase.xy), 1.0.xx);
    float2 ndc = ((pixel + 0.5.xx) / size) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    if (projection_alpha.z > 0.5) {
        origin = position_near.xyz +
            right_aspect.xyz * ndc.x * projection_alpha.x +
            up_tan_half_fov.xyz * ndc.y * projection_alpha.y;
        direction = normalize(forward_far.xyz);
    } else {
        origin = position_near.xyz;
        direction = normalize(
            forward_far.xyz +
            right_aspect.xyz * ndc.x * up_tan_half_fov.w * right_aspect.w +
            up_tan_half_fov.xyz * ndc.y * up_tan_half_fov.w);
    }
}

bool ProjectPrevious(float3 world, out float2 uv, out float expected_depth)
{
    float3 delta = world - PreviousPositionNear.xyz;
    float forward_depth = dot(delta, normalize(PreviousForwardFar.xyz));
    if (forward_depth <= PreviousPositionNear.w) return false;

    float2 ndc;
    if (PreviousProjectionAlpha.z > 0.5) {
        ndc.x = dot(delta, PreviousRightAspect.xyz) /
            max(PreviousProjectionAlpha.x, 1.0e-6);
        ndc.y = dot(delta, PreviousUpTanHalfFov.xyz) /
            max(PreviousProjectionAlpha.y, 1.0e-6);
    } else {
        ndc.x = dot(delta, PreviousRightAspect.xyz) /
            max(forward_depth * PreviousUpTanHalfFov.w * PreviousRightAspect.w, 1.0e-6);
        ndc.y = dot(delta, PreviousUpTanHalfFov.xyz) /
            max(forward_depth * PreviousUpTanHalfFov.w, 1.0e-6);
    }

    uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (!all(uv >= 0.0.xx) || !all(uv <= 1.0.xx)) return false;

    float2 previous_pixel =
        uv * max(float2(ResolutionGridPhase.xy), 1.0.xx) - 0.5.xx;
    float3 previous_origin, previous_direction;
    CameraRay(
        previous_pixel,
        PreviousPositionNear,
        PreviousForwardFar,
        PreviousRightAspect,
        PreviousUpTanHalfFov,
        PreviousProjectionAlpha,
        previous_origin,
        previous_direction);
    expected_depth = dot(world - previous_origin, previous_direction);
    return expected_depth > 0.0;
}

bool TemporalSample(
    uint2 pixel,
    out float4 color,
    out float depth,
    out float4 surface,
    out float age)
{
    color = 0.0.xxxx;
    depth = 0.0;
    surface = 0.0.xxxx;
    age = Params.x;
    if (Control.x == 0u || Control.y == 0u) return false;

    float2 uv = PixelUv(pixel);
    float seed_depth = PreviousDepth.SampleLevel(PreviousDepthSampler, uv, 0.0);
    float seed_age = PreviousAge.SampleLevel(PreviousAgeSampler, uv, 0.0);
    if (seed_depth <= 0.0 || seed_age > Params.x) return false;

    float2 previous_uv = uv;
    float expected_depth = seed_depth;
    if (Control.z != 0u) {
        float3 current_origin, current_direction;
        CameraRay(
            float2(pixel),
            CurrentPositionNear,
            CurrentForwardFar,
            CurrentRightAspect,
            CurrentUpTanHalfFov,
            CurrentProjectionAlpha,
            current_origin,
            current_direction);
        const float3 approximate_world = current_origin + current_direction * seed_depth;
        if (!ProjectPrevious(approximate_world, previous_uv, expected_depth)) return false;
    }

    const float sampled_depth = PreviousDepth.SampleLevel(
        PreviousDepthSampler, previous_uv, 0.0);
    const float sampled_age = PreviousAge.SampleLevel(
        PreviousAgeSampler, previous_uv, 0.0);
    if (sampled_depth <= 0.0 || sampled_age > Params.x) return false;

    const float tolerance = max(Params.y * max(expected_depth, 1.0), 0.0025);
    if (abs(sampled_depth - expected_depth) > tolerance) return false;

    const float4 reference_surface = PreviousSurface.SampleLevel(
        PreviousSurfaceSampler, uv, 0.0);
    const float4 sampled_surface = PreviousSurface.SampleLevel(
        PreviousSurfaceSampler, previous_uv, 0.0);
    if (reference_surface.w > 0.0 && sampled_surface.w > 0.0) {
        if (abs(reference_surface.w - sampled_surface.w) > 0.25) return false;
        const float3 a = normalize(reference_surface.xyz);
        const float3 b = normalize(sampled_surface.xyz);
        if (dot(a, b) < Params.z) return false;
    }

    color = PreviousColor.SampleLevel(PreviousColorSampler, previous_uv, 0.0);
    depth = sampled_depth;
    surface = sampled_surface;
    age = sampled_age + 1.0;
    return true;
}

void LatticeCell(
    uint2 pixel,
    out int2 p00,
    out int2 p10,
    out int2 p01,
    out int2 p11,
    out float2 blend)
{
    int grid = int(max(ResolutionGridPhase.z, 1u));
    int phase = int(ResolutionGridPhase.w % max(ResolutionGridPhase.z * ResolutionGridPhase.z, 1u));
    int2 offset = int2(phase % grid, phase / grid);
    int2 size = max(int2(ResolutionGridPhase.xy), int2(1, 1));
    int2 maximum_pixel = size - int2(1, 1);
    int2 maximum_cell = max((maximum_pixel - offset) / grid, int2(0, 0));
    float2 lattice = (float2(pixel) - float2(offset)) / float(grid);
    float2 clamped = clamp(lattice, 0.0.xx, float2(maximum_cell));
    int2 c0 = int2(floor(clamped));
    int2 c1 = min(c0 + int2(1, 1), maximum_cell);
    blend = clamped - float2(c0);
    p00 = clamp(c0 * grid + offset, int2(0, 0), maximum_pixel);
    p10 = clamp(int2(c1.x, c0.y) * grid + offset, int2(0, 0), maximum_pixel);
    p01 = clamp(int2(c0.x, c1.y) * grid + offset, int2(0, 0), maximum_pixel);
    p11 = clamp(c1 * grid + offset, int2(0, 0), maximum_pixel);
}

float CandidateWeight(
    int2 candidate,
    float bilinear,
    float expected_depth,
    float4 expected_surface)
{
    if (candidate.x < 0 || candidate.y < 0 ||
        candidate.x >= int(ResolutionGridPhase.x) ||
        candidate.y >= int(ResolutionGridPhase.y)) return 0.0;
    if (!Scheduled(uint2(candidate))) return 0.0;

    float2 uv = (float2(candidate) + 0.5.xx) /
        max(float2(ResolutionGridPhase.xy), 1.0.xx);
    float depth = FreshDepth.SampleLevel(FreshDepthSampler, uv, 0.0);
    if (depth <= 0.0) return bilinear;

    float weight = bilinear;
    if (expected_depth > 0.0) {
        float scale = max(expected_depth, 1.0);
        weight *= exp(-abs(depth - expected_depth) / max(scale * Params.y, 0.0025));
    }

    float4 surface = FreshSurface.SampleLevel(FreshSurfaceSampler, uv, 0.0);
    if (expected_surface.w > 0.0 && surface.w > 0.0) {
        if (abs(expected_surface.w - surface.w) > 0.25) weight *= 0.05;
        else if (dot(normalize(expected_surface.xyz), normalize(surface.xyz)) < Params.z)
            weight *= 0.10;
    }
    return weight;
}

void SpatialSample(
    uint2 pixel,
    float expected_depth,
    float4 expected_surface,
    out float4 color,
    out float depth,
    out float4 surface)
{
    int2 p00, p10, p01, p11;
    float2 blend;
    LatticeCell(pixel, p00, p10, p01, p11, blend);
    int2 points[4] = {p00, p10, p01, p11};
    float bilinear[4] = {
        (1.0 - blend.x) * (1.0 - blend.y),
        blend.x * (1.0 - blend.y),
        (1.0 - blend.x) * blend.y,
        blend.x * blend.y
    };

    float4 color_sum = 0.0.xxxx;
    float depth_sum = 0.0;
    float4 surface_sum = 0.0.xxxx;
    float total = 0.0;
    [unroll] for (uint index = 0u; index < 4u; ++index) {
        float weight = CandidateWeight(points[index], bilinear[index], expected_depth, expected_surface);
        if (weight <= 0.0) continue;
        float2 uv = (float2(points[index]) + 0.5.xx) /
            max(float2(ResolutionGridPhase.xy), 1.0.xx);
        color_sum += FreshColor.SampleLevel(FreshColorSampler, uv, 0.0) * weight;
        depth_sum += FreshDepth.SampleLevel(FreshDepthSampler, uv, 0.0) * weight;
        surface_sum += FreshSurface.SampleLevel(FreshSurfaceSampler, uv, 0.0) * weight;
        total += weight;
    }

    if (total <= 1.0e-6) {
        int2 maximum_pixel = max(int2(ResolutionGridPhase.xy) - int2(1, 1), int2(0, 0));
        int2 fallback = clamp(p00, int2(0, 0), maximum_pixel);
        float2 uv = (float2(fallback) + 0.5.xx) /
            max(float2(ResolutionGridPhase.xy), 1.0.xx);
        color = FreshColor.SampleLevel(FreshColorSampler, uv, 0.0);
        depth = FreshDepth.SampleLevel(FreshDepthSampler, uv, 0.0);
        surface = FreshSurface.SampleLevel(FreshSurfaceSampler, uv, 0.0);
        return;
    }

    color = color_sum / total;
    depth = depth_sum / total;
    surface = surface_sum / total;
}

[numthreads(8, 8, 1)]
void Main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= ResolutionGridPhase.x || tid.y >= ResolutionGridPhase.y) return;
    const uint2 pixel = tid.xy;

    float4 color;
    float depth;
    float4 surface;
    float age;
    uint source = 0u;

    if (Scheduled(pixel)) {
        const float2 uv = PixelUv(pixel);
        color = FreshColor.SampleLevel(FreshColorSampler, uv, 0.0);
        depth = FreshDepth.SampleLevel(FreshDepthSampler, uv, 0.0);
        surface = FreshSurface.SampleLevel(FreshSurfaceSampler, uv, 0.0);
        age = 0.0;
        source = 1u;
    } else {
        float4 temporal_color;
        float temporal_depth;
        float4 temporal_surface;
        float temporal_age;
        const bool temporal = TemporalSample(
            pixel,
            temporal_color,
            temporal_depth,
            temporal_surface,
            temporal_age);
        if (temporal) {
            color = temporal_color;
            depth = temporal_depth;
            surface = temporal_surface;
            age = temporal_age;
            source = 2u;
        } else {
            float2 uv = PixelUv(pixel);
            float expected_depth = Control.y != 0u
                ? PreviousDepth.SampleLevel(PreviousDepthSampler, uv, 0.0)
                : 0.0;
            float4 expected_surface = Control.y != 0u
                ? PreviousSurface.SampleLevel(PreviousSurfaceSampler, uv, 0.0)
                : 0.0.xxxx;
            SpatialSample(pixel, expected_depth, expected_surface, color, depth, surface);
            age = min(Params.x, 1.0 + (Control.y != 0u
                ? PreviousAge.SampleLevel(PreviousAgeSampler, uv, 0.0)
                : 0.0));
            source = 3u;
        }
    }

    if (Control.w != 0u) {
        if (source == 1u) color = float4(0.15, 1.0, 0.15, 1.0);
        else if (source == 2u) color = float4(0.15, 0.45, 1.0, 1.0);
        else color = float4(1.0, 0.75, 0.15, 1.0);
    }

    OutputColor[pixel] = color;
    OutputDepth[pixel] = depth;
    NextColor[pixel] = color;
    NextDepth[pixel] = depth;
    NextSurface[pixel] = surface;
    NextAge[pixel] = age;
}
)HLSL";

} // namespace Renderer::SDLGPU::ReconstructionShaders

#endif
