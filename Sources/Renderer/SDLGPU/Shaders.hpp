#ifndef HORSE_RENDERER_SDLGPU_SHADERS_HPP
#define HORSE_RENDERER_SDLGPU_SHADERS_HPP

#include "Renderer/SDLGPU/PBRShaders.hpp"

namespace Renderer::SDLGPU::Shaders {

inline constexpr const char *Shadow = R"HLSL(
struct RasterVertex {
    float4 position;
    float4 normal;
    float4 uv;
    uint4 meta;
};

struct RasterItem {
    float4 model0; float4 model1; float4 model2; float4 model3;
    float4 inverse0; float4 inverse1; float4 inverse2; float4 inverse3;
    uint4 meta;
    uint4 flags;
};

struct RasterInfluence {
    uint joint;
    float weight;
    uint2 reserved;
};

StructuredBuffer<RasterVertex> Vertices : register(t0, space0);
StructuredBuffer<RasterItem> Items : register(t1, space0);
StructuredBuffer<RasterInfluence> Influences : register(t2, space0);
StructuredBuffer<float4> SkinMatrices : register(t3, space0);
cbuffer ShadowView : register(b0, space1) {
    float4 ViewPositionNear;
    float4 ViewForwardFar;
    float4 ViewRightScale;
    float4 ViewUpScale;
    float4 ViewMeta;
};

float3 TransformPoint(float4 c0, float4 c1, float4 c2, float4 c3, float3 p) {
    return c0.xyz * p.x + c1.xyz * p.y + c2.xyz * p.z + c3.xyz;
}

float3 TransformSkinPoint(uint matrix_index, float3 p) {
    uint base = matrix_index * 4u;
    return SkinMatrices[base + 0u].xyz * p.x +
        SkinMatrices[base + 1u].xyz * p.y +
        SkinMatrices[base + 2u].xyz * p.z +
        SkinMatrices[base + 3u].xyz;
}

float3 SkinPosition(RasterVertex vertex, RasterItem item) {
    if (item.meta.w == 0u || vertex.meta.z == 0u) return vertex.position.xyz;
    float3 position = 0.0.xxx;
    float total = 0.0;
    for (uint index = 0u; index < vertex.meta.z; ++index) {
        RasterInfluence influence = Influences[vertex.meta.y + index];
        if (influence.weight == 0.0 || influence.joint >= item.meta.w) continue;
        position += TransformSkinPoint(item.meta.z + influence.joint, vertex.position.xyz) * influence.weight;
        total += influence.weight;
    }
    return total > 1.0e-8 ? position / total : vertex.position.xyz;
}

float4 VSMain(uint vertex_id : SV_VertexID) : SV_Position {
    RasterVertex vertex = Vertices[vertex_id];
    RasterItem item = Items[vertex.meta.x];
    if (item.flags.x == 0u) return float4(2.0, 2.0, 2.0, 1.0);
    float3 local = SkinPosition(vertex, item);
    float3 position = TransformPoint(
        item.model0, item.model1, item.model2, item.model3, local);
    float3 delta = position - ViewPositionNear.xyz;
    float depth = dot(delta, ViewForwardFar.xyz);

    if (ViewMeta.x > 0.5) {
        float x = dot(delta, ViewRightScale.xyz) / max(ViewRightScale.w, 1.0e-6);
        float y = dot(delta, ViewUpScale.xyz) / max(ViewUpScale.w, 1.0e-6);
        float z = saturate((depth - ViewPositionNear.w) /
            max(ViewForwardFar.w - ViewPositionNear.w, 1.0e-5));
        return float4(x, y, z, 1.0);
    }

    float x = dot(delta, ViewRightScale.xyz) /
        max(ViewUpScale.w * ViewRightScale.w, 1.0e-6);
    float y = dot(delta, ViewUpScale.xyz) / max(ViewUpScale.w, 1.0e-6);
    float near_z = ViewPositionNear.w;
    float far_z = ViewForwardFar.w;
    float z = (far_z * depth - near_z * far_z) / max(far_z - near_z, 1.0e-5);
    return float4(x, y, z, depth);
}

void PSMain() {}
)HLSL";

inline constexpr const char *Depth = R"HLSL(
void PSMain() {}
)HLSL";

inline constexpr const char *Raster = PBRShaders::Raster;

inline constexpr const char *Sky = R"HLSL(
Texture2D<float4> SkyTexture : register(t0, space2);
SamplerState SkySampler : register(s0, space2);
StructuredBuffer<float4> GI : register(t1, space2);
cbuffer SkyFrame : register(b0, space3) {
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
struct VSOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VSOut SkyVS(uint id : SV_VertexID) {
    float2 p = id == 0u ? float2(-1,-1) : (id == 1u ? float2(3,-1) : float2(-1,3));
    VSOut o; o.position=float4(p,0.999999,1); o.uv=p*0.5+0.5; return o;
}
float3 SrgbToLinear(float3 color) {
    color = saturate(color);
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(low, high, step(0.04045.xxx, color));
}
struct PSOut { float4 color : SV_Target0; float2 velocity : SV_Target1; };
PSOut SkyPS(VSOut i) {
    float2 ndc = i.uv * 2.0 - 1.0;
    float3 direction;
    if (ProjectionAlpha.z > 0.5) direction = normalize(CameraForwardFar.xyz);
    else direction = normalize(CameraForwardFar.xyz +
        CameraRightAspect.xyz * ndc.x * CameraUpTanHalfFov.w * CameraRightAspect.w +
        CameraUpTanHalfFov.xyz * ndc.y * CameraUpTanHalfFov.w);
    float3 color = GI[4].xyz * max(GI[3].w, 1.0);
    if (GI[4].w > 0.5) {
        const float PI=3.14159265359;
        float u=frac(atan2(direction.z,direction.x)/(2*PI)+0.5+GI[7].w/(2*PI));
        float v=acos(clamp(direction.y,-1.0,1.0))/PI;
        color=SrgbToLinear(SkyTexture.Sample(SkySampler,float2(u,v)).rgb)*max(GI[3].w,0.0);
    }
    PSOut o; o.color=float4(max(color,0.0.xxx),1); o.velocity=0.0.xx; return o;
}
)HLSL";

} // namespace Renderer::SDLGPU::Shaders

#endif
