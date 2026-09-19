#ifndef HORSE_RENDERER_INTERNAL_VOLUMETRIC_MARCH_SHADER_HPP
#define HORSE_RENDERER_INTERNAL_VOLUMETRIC_MARCH_SHADER_HPP

namespace Renderer::Internal {

inline constexpr const char *VolumetricMarchShader = R"HLSL(
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
struct GpuInstance {
    float4x4 object_to_world;
    float4x4 world_to_object;
    float4 bounds_min;
    float4 bounds_max;
    uint4 data;
};

Texture2D<float> SceneDepth : register(t0, space0);
SamplerState DepthSampler : register(s0, space0);
StructuredBuffer<GpuNode> TlasNodes : register(t1, space0);
StructuredBuffer<GpuInstance> Instances : register(t2, space0);
StructuredBuffer<GpuNode> BlasNodes : register(t3, space0);
StructuredBuffer<uint4> Blases : register(t4, space0);
StructuredBuffer<GpuTriangle> LocalTriangles : register(t5, space0);
StructuredBuffer<GpuNode> DynamicNodes : register(t6, space0);
StructuredBuffer<GpuTriangle> DynamicTriangles : register(t7, space0);
StructuredBuffer<float4> Shading : register(t8, space0);
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
cbuffer VolumetricData : register(b1, space2) {
    float Density;
    float Anisotropy;
    float MaximumDistance;
    float DepthMode;
    uint SampleCount;
    uint FrameIndex;
    float Jitter;
    float Padding;
};
cbuffer AccelerationData : register(b2, space2) {
    uint4 AccelerationCounts0;
    uint4 AccelerationCounts1;
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

float3 SafeInverse(float3 direction)
{
    float3 safe_direction = sign(direction + 1.0e-20.xxx) * max(abs(direction), 1.0e-8.xxx);
    return 1.0 / safe_direction;
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

bool OccludedBlas(GpuInstance instance, float3 ro, float3 rd, float max_t)
{
    uint blas_index = instance.data.z;
    if (blas_index >= AccelerationCounts0.w) return false;
    uint4 blas = Blases[blas_index];
    if (blas.y == 0u || blas.w == 0u) return false;

    float3 local_ro = mul(instance.world_to_object, float4(ro, 1.0)).xyz;
    float3 local_rd = mul(instance.world_to_object, float4(rd, 0.0)).xyz;
    float3 inv_rd = SafeInverse(local_rd);

    uint stack[64];
    uint top = 0u;
    stack[top++] = blas.x;
    uint node_end = min(blas.x + blas.y, AccelerationCounts0.z);
    uint triangle_end = min(blas.z + blas.w, AccelerationCounts1.x);

    while (top > 0u) {
        uint index = stack[--top];
        if (index < blas.x || index >= node_end) continue;
        GpuNode node = BlasNodes[index];
        if (!IntersectAabb(local_ro, inv_rd, node.minimum, node.maximum, max_t)) continue;

        if ((node.meta & 0x80000000u) != 0u) {
            uint count = node.meta & 0x7fffffffu;
            for (uint i = 0u; i < count; ++i) {
                uint triangle = node.first + i;
                if (triangle >= blas.z && triangle < triangle_end &&
                    IntersectTriangle(local_ro, local_rd, LocalTriangles[triangle], max_t))
                    return true;
            }
        } else if (top + 2u <= 64u) {
            stack[top++] = node.first;
            stack[top++] = node.meta;
        }
    }
    return false;
}

bool OccludedTlas(float3 ro, float3 rd, float max_t)
{
    if (AccelerationCounts0.x == 0u || AccelerationCounts0.y == 0u) return false;
    float3 inv_rd = SafeInverse(rd);
    uint stack[64];
    uint top = 0u;
    stack[top++] = 0u;

    while (top > 0u) {
        uint index = stack[--top];
        if (index >= AccelerationCounts0.x) continue;
        GpuNode node = TlasNodes[index];
        if (!IntersectAabb(ro, inv_rd, node.minimum, node.maximum, max_t)) continue;

        if ((node.meta & 0x80000000u) != 0u) {
            uint count = node.meta & 0x7fffffffu;
            for (uint i = 0u; i < count; ++i) {
                uint instance_index = node.first + i;
                if (instance_index >= AccelerationCounts0.y) break;
                GpuInstance instance = Instances[instance_index];
                if (!IntersectAabb(
                        ro,
                        inv_rd,
                        instance.bounds_min.xyz,
                        instance.bounds_max.xyz,
                        max_t))
                    continue;
                if (OccludedBlas(instance, ro, rd, max_t)) return true;
            }
        } else if (top + 2u <= 64u) {
            stack[top++] = node.first;
            stack[top++] = node.meta;
        }
    }
    return false;
}

bool OccludedDynamic(float3 ro, float3 rd, float max_t)
{
    if (AccelerationCounts1.y == 0u || AccelerationCounts1.z == 0u) return false;
    float3 inv_rd = SafeInverse(rd);
    uint stack[64];
    uint top = 0u;
    stack[top++] = 0u;

    while (top > 0u) {
        uint index = stack[--top];
        if (index >= AccelerationCounts1.y) continue;
        GpuNode node = DynamicNodes[index];
        if (!IntersectAabb(ro, inv_rd, node.minimum, node.maximum, max_t)) continue;

        if ((node.meta & 0x80000000u) != 0u) {
            uint count = node.meta & 0x7fffffffu;
            for (uint i = 0u; i < count; ++i) {
                uint triangle = node.first + i;
                if (triangle < AccelerationCounts1.z &&
                    IntersectTriangle(ro, rd, DynamicTriangles[triangle], max_t))
                    return true;
            }
        } else if (top + 2u <= 64u) {
            stack[top++] = node.first;
            stack[top++] = node.meta;
        }
    }
    return false;
}

bool Occluded(float3 ro, float3 rd, float max_t)
{
    if (max_t <= 0.0) return false;
    return OccludedTlas(ro, rd, max_t) || OccludedDynamic(ro, rd, max_t);
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

} // namespace Renderer::Internal

#endif
