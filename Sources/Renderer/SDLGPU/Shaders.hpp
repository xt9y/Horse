#ifndef HORSE_RENDERER_SDLGPU_SHADERS_HPP
#define HORSE_RENDERER_SDLGPU_SHADERS_HPP

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

float3 TransformVector(uint matrix_index, float3 v) {
    uint base = matrix_index * 4u;
    return SkinMatrices[base + 0u].xyz * v.x +
        SkinMatrices[base + 1u].xyz * v.y +
        SkinMatrices[base + 2u].xyz * v.z;
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
inline constexpr const char *Raster = R"HLSL(
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

struct GpuBaseMaterial { float4 base_color; int4 data; };
struct GpuMaterial {
    float4 emissive_strength;
    float4 pbr;
    float4 specular;
    float4 clearcoat_sheen;
    float4 sheen_thickness;
    float4 attenuation;
    float4 diffuse_transmission;
    float4 anisotropy_iridescence;
    float4 iridescence_dispersion_ior;
    float4 misc;
    int4 tex0; int4 tex1; int4 tex2; int4 tex3; int4 tex4; int4 tex5;
};

StructuredBuffer<RasterVertex> VVertices : register(t0, space0);
StructuredBuffer<RasterItem> VItems : register(t1, space0);
StructuredBuffer<RasterInfluence> VInfluences : register(t2, space0);
StructuredBuffer<float4> VSkinMatrices : register(t3, space0);
StructuredBuffer<uint> VVisibility : register(t4, space0);
cbuffer VertexFrame : register(b0, space1) {
    float4 VCameraPositionNear;
    float4 VCameraForwardFar;
    float4 VCameraRightAspect;
    float4 VCameraUpTanHalfFov;
    float4 VProjectionAlpha;
    float4 VResolution;
    int4 VCounts;
    uint4 VFrame;
    uint4 VPathPolicy;
};
cbuffer VertexDraw : register(b1, space1) {
    uint4 VDraw;
};

Texture2D<float4> Tex0  : register(t0,  space2); SamplerState Samp0  : register(s0,  space2);
Texture2D<float4> Tex1  : register(t1,  space2); SamplerState Samp1  : register(s1,  space2);
Texture2D<float4> Tex2  : register(t2,  space2); SamplerState Samp2  : register(s2,  space2);
Texture2D<float4> Tex3  : register(t3,  space2); SamplerState Samp3  : register(s3,  space2);
Texture2D<float4> Tex4  : register(t4,  space2); SamplerState Samp4  : register(s4,  space2);
Texture2D<float4> Tex5  : register(t5,  space2); SamplerState Samp5  : register(s5,  space2);
Texture2D<float4> Tex6  : register(t6,  space2); SamplerState Samp6  : register(s6,  space2);
Texture2D<float4> Tex7  : register(t7,  space2); SamplerState Samp7  : register(s7,  space2);
Texture2D<float4> Tex8  : register(t8,  space2); SamplerState Samp8  : register(s8,  space2);
Texture2D<float4> Tex9  : register(t9,  space2); SamplerState Samp9  : register(s9,  space2);
Texture2D<float4> Tex10 : register(t10, space2); SamplerState Samp10 : register(s10, space2);
Texture2D<float4> Tex11 : register(t11, space2); SamplerState Samp11 : register(s11, space2);
Texture2D<float4> Tex12 : register(t12, space2); SamplerState Samp12 : register(s12, space2);
Texture2D<float4> Tex13 : register(t13, space2); SamplerState Samp13 : register(s13, space2);
Texture2D<float4> Tex14 : register(t14, space2); SamplerState Samp14 : register(s14, space2);
Texture2DArray<float> ShadowMaps : register(t15, space2);
SamplerComparisonState ShadowSampler : register(s15, space2);

StructuredBuffer<GpuBaseMaterial> PBaseMaterials : register(t16, space2);
StructuredBuffer<GpuMaterial> PMaterials : register(t17, space2);
StructuredBuffer<float4> PGI : register(t18, space2);
StructuredBuffer<float4> PShadows : register(t19, space2);
cbuffer PixelFrame : register(b0, space3) {
    float4 PCameraPositionNear;
    float4 PCameraForwardFar;
    float4 PCameraRightAspect;
    float4 PCameraUpTanHalfFov;
    float4 PProjectionAlpha;
    float4 PResolution;
    int4 PCounts;
    uint4 PFrame;
    uint4 PPathPolicy;
};

float4 SampleSlot(int slot, float2 uv) {
    if (slot == 0) return Tex0.Sample(Samp0, uv);
    if (slot == 1) return Tex1.Sample(Samp1, uv);
    if (slot == 2) return Tex2.Sample(Samp2, uv);
    if (slot == 3) return Tex3.Sample(Samp3, uv);
    if (slot == 4) return Tex4.Sample(Samp4, uv);
    if (slot == 5) return Tex5.Sample(Samp5, uv);
    if (slot == 6) return Tex6.Sample(Samp6, uv);
    if (slot == 7) return Tex7.Sample(Samp7, uv);
    if (slot == 8) return Tex8.Sample(Samp8, uv);
    if (slot == 9) return Tex9.Sample(Samp9, uv);
    if (slot == 10) return Tex10.Sample(Samp10, uv);
    if (slot == 11) return Tex11.Sample(Samp11, uv);
    if (slot == 12) return Tex12.Sample(Samp12, uv);
    if (slot == 13) return Tex13.Sample(Samp13, uv);
    if (slot == 14) return Tex14.Sample(Samp14, uv);
    return 1.0.xxxx;
}

float3 SrgbToLinear(float3 color) {
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(low, high, step(0.04045.xxx, color));
}

float4 SampleColorSlot(int slot, float2 uv) {
    float4 sample = SampleSlot(slot, uv);
    sample.rgb = SrgbToLinear(sample.rgb);
    return sample;
}

struct VSOut {
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint material : TEXCOORD3;
};

float3 RasterTransformPoint(float4 c0, float4 c1, float4 c2, float4 c3, float3 p) {
    return c0.xyz * p.x + c1.xyz * p.y + c2.xyz * p.z + c3.xyz;
}

float3 RasterSkinPoint(uint matrix_index, float3 p) {
    uint base = matrix_index * 4u;
    return VSkinMatrices[base + 0u].xyz * p.x +
        VSkinMatrices[base + 1u].xyz * p.y +
        VSkinMatrices[base + 2u].xyz * p.z +
        VSkinMatrices[base + 3u].xyz;
}

float3 RasterSkinVector(uint matrix_index, float3 v) {
    uint base = matrix_index * 4u;
    return VSkinMatrices[base + 0u].xyz * v.x +
        VSkinMatrices[base + 1u].xyz * v.y +
        VSkinMatrices[base + 2u].xyz * v.z;
}

void RasterSkin(
    RasterVertex vertex,
    RasterItem item,
    out float3 position,
    out float3 normal)
{
    position = vertex.position.xyz;
    normal = vertex.normal.xyz;
    if (item.meta.w == 0u || vertex.meta.z == 0u) return;

    float3 skinned_position = 0.0.xxx;
    float3 skinned_normal = 0.0.xxx;
    float total = 0.0;
    for (uint index = 0u; index < vertex.meta.z; ++index) {
        RasterInfluence influence = VInfluences[vertex.meta.y + index];
        if (influence.weight == 0.0 || influence.joint >= item.meta.w) continue;
        uint matrix_index = item.meta.z + influence.joint;
        skinned_position += RasterSkinPoint(matrix_index, position) * influence.weight;
        skinned_normal += RasterSkinVector(matrix_index, normal) * influence.weight;
        total += influence.weight;
    }
    if (total > 1.0e-8) {
        position = skinned_position / total;
        normal = normalize(skinned_normal / total);
    }
}

float3 RasterWorldNormal(RasterItem item, float3 normal) {
    return normalize(float3(
        dot(item.inverse0.xyz, normal),
        dot(item.inverse1.xyz, normal),
        dot(item.inverse2.xyz, normal)));
}

VSOut VSMain(uint vertex_id : SV_VertexID) {
    VSOut o;
    RasterVertex vertex = VVertices[vertex_id + VDraw.x];
    RasterItem item = VItems[vertex.meta.x];
    uint entity = item.meta.y;
    if (item.flags.x != 0u && VVisibility[entity] == 0u) {
        o.position = float4(2.0, 2.0, 2.0, 1.0);
        o.world = 0.0.xxx;
        o.normal = float3(0.0, 1.0, 0.0);
        o.uv = 0.0.xx;
        o.material = 0u;
        return o;
    }
    float3 local_position;
    float3 local_normal;
    RasterSkin(vertex, item, local_position, local_normal);
    float3 p = RasterTransformPoint(
        item.model0, item.model1, item.model2, item.model3, local_position);
    float3 n = RasterWorldNormal(item, local_normal);
    float2 uv = vertex.uv.xy;

    float3 delta = p - VCameraPositionNear.xyz;
    float depth = dot(delta, VCameraForwardFar.xyz);
    if (VProjectionAlpha.z > 0.5) {
        float x = dot(delta, VCameraRightAspect.xyz) / VProjectionAlpha.x;
        float y = dot(delta, VCameraUpTanHalfFov.xyz) / VProjectionAlpha.y;
        float z = saturate((depth - VCameraPositionNear.w) /
            max(VCameraForwardFar.w - VCameraPositionNear.w, 1.0e-5));
        o.position = float4(x, y, z, 1.0);
    } else {
        float x = dot(delta, VCameraRightAspect.xyz) /
            max(VCameraUpTanHalfFov.w * VCameraRightAspect.w, 1.0e-6);
        float y = dot(delta, VCameraUpTanHalfFov.xyz) / max(VCameraUpTanHalfFov.w, 1.0e-6);
        float near_z = VCameraPositionNear.w;
        float far_z = VCameraForwardFar.w;
        float z = far_z < 3.0e37
            ? (far_z * depth - near_z * far_z) / max(far_z - near_z, 1.0e-5)
            : depth - near_z;
        o.position = float4(x, y, z, depth);
    }
    o.world = p;
    o.normal = n;
    o.uv = uv;
    o.material = item.meta.x;
    return o;
}

float3 SampleGI(float3 position, float3 normal) {
    if (PGI[0].w < 0.5 || PGI[2].w < 1.0) return 0.0.xxx;
    float3 minimum = PGI[0].xyz;
    float3 maximum = PGI[1].xyz;
    float intensity = max(PGI[1].w, 0.0);
    uint3 size = (uint3)max(PGI[2].xyz, 1.0.xxx);
    float3 span = max(maximum - minimum, 1.0e-6.xxx);
    float3 g = saturate((position - minimum) / span) * (float3(size) - 1.0);
    uint3 p0 = (uint3)floor(g);
    uint3 p1 = min(p0 + 1u, size - 1u);
    float3 f = g - float3(p0);
    float3 coeff[4];
    [unroll] for (uint c = 0u; c < 4u; ++c) {
        uint i000 = p0.x + size.x * (p0.y + size.y * p0.z);
        uint i100 = p1.x + size.x * (p0.y + size.y * p0.z);
        uint i010 = p0.x + size.x * (p1.y + size.y * p0.z);
        uint i110 = p1.x + size.x * (p1.y + size.y * p0.z);
        uint i001 = p0.x + size.x * (p0.y + size.y * p1.z);
        uint i101 = p1.x + size.x * (p0.y + size.y * p1.z);
        uint i011 = p0.x + size.x * (p1.y + size.y * p1.z);
        uint i111 = p1.x + size.x * (p1.y + size.y * p1.z);
        float3 a = lerp(PGI[12u + i000*4u+c].xyz, PGI[12u + i100*4u+c].xyz, f.x);
        float3 b = lerp(PGI[12u + i010*4u+c].xyz, PGI[12u + i110*4u+c].xyz, f.x);
        float3 d = lerp(PGI[12u + i001*4u+c].xyz, PGI[12u + i101*4u+c].xyz, f.x);
        float3 e = lerp(PGI[12u + i011*4u+c].xyz, PGI[12u + i111*4u+c].xyz, f.x);
        coeff[c] = lerp(lerp(a,b,f.y), lerp(d,e,f.y), f.z);
    }
    float3 n = normalize(normal);
    const float PI = 3.14159265359;
    const float Y00 = 0.28209479177;
    const float Y1 = 0.48860251190;
    float3 result = coeff[0] * (PI * Y00);
    result += coeff[1] * ((2.0*PI/3.0)*Y1*n.x);
    result += coeff[2] * ((2.0*PI/3.0)*Y1*n.y);
    result += coeff[3] * ((2.0*PI/3.0)*Y1*n.z);
    return max(result, 0.0.xxx) * intensity;
}

float3 EnvironmentColor(float3 direction) {
    float3 sky = PGI[4].xyz;
    float intensity = max(PGI[3].w, 0.0);
    if (PGI[4].w > 0.5) {
        const float PI = 3.14159265359;
        float rotation = PGI[7].w;
        float u = frac(atan2(direction.z, direction.x) / (2.0*PI) + 0.5 + rotation/(2.0*PI));
        float v = acos(clamp(direction.y, -1.0, 1.0)) / PI;
        return SampleColorSlot(0, float2(u,v)).rgb * intensity;
    }
    return sky * max(intensity, 1.0);
}

float3 ApplyNormalMap(float3 n, float3 position, float2 uv, int slot, float scale) {
    if (slot < 0) return normalize(n);
    float3 mapped = SampleSlot(slot, uv).xyz * 2.0 - 1.0;
    mapped.xy *= scale;
    float3 dp1 = ddx(position), dp2 = ddy(position);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 t = normalize(dp1 * duv2.y - dp2 * duv1.y);
    float3 b = normalize(-dp1 * duv2.x + dp2 * duv1.x);
    if (!all(isfinite(t)) || !all(isfinite(b))) return normalize(n);
    return normalize(t * mapped.x + b * mapped.y + normalize(n) * mapped.z);
}

uint ShadowLightCount() {
    return (uint)max(PShadows[0].x, 0.0);
}

uint ShadowViewBase() {
    return 1u + ShadowLightCount();
}
float4 ShadowView(uint view_index, uint field) {
    return PShadows[ShadowViewBase() + view_index * 5u + field];
}

float SampleShadow(uint view_index, float3 position, float bias) {
    float4 position_near = ShadowView(view_index, 0u);
    float4 forward_far = ShadowView(view_index, 1u);
    float4 right_scale = ShadowView(view_index, 2u);
    float4 up_scale = ShadowView(view_index, 3u);
    float4 meta = ShadowView(view_index, 4u);
    float3 delta = position - position_near.xyz;
    float depth = dot(delta, forward_far.xyz);
    if (depth <= position_near.w || depth >= forward_far.w) return 1.0;

    float2 ndc;
    float reference;
    if (meta.x > 0.5) {
        ndc.x = dot(delta, right_scale.xyz) / max(right_scale.w, 1.0e-6);
        ndc.y = dot(delta, up_scale.xyz) / max(up_scale.w, 1.0e-6);
        reference = (depth - position_near.w) /
            max(forward_far.w - position_near.w, 1.0e-5);
    } else {
        ndc.x = dot(delta, right_scale.xyz) /
            max(up_scale.w * right_scale.w * depth, 1.0e-6);
        ndc.y = dot(delta, up_scale.xyz) / max(up_scale.w * depth, 1.0e-6);
        reference = (forward_far.w * depth - position_near.w * forward_far.w) /
            max((forward_far.w - position_near.w) * depth, 1.0e-5);
    }
    if (any(abs(ndc) > 1.0.xx)) return 1.0;

    float depth_bias = max(bias, 0.0) /
        max(forward_far.w - position_near.w, 1.0e-4);
    float3 sample_position = float3(
        ndc.x * 0.5 + 0.5,
        0.5 - ndc.y * 0.5,
        (float)((uint)meta.y)
    );
    return ShadowMaps.SampleCmpLevelZero(
        ShadowSampler,
        sample_position,
        saturate(reference - depth_bias)
    );
}

float ShadowVisibility(uint light_index, float3 position, float type, float bias) {
    uint light_count = ShadowLightCount();
    if (light_index >= light_count) return 1.0;
    float4 record = PShadows[1u + light_index];
    uint first = (uint)max(record.x, 0.0);
    uint count = (uint)max(record.y, 0.0);
    if (count == 0u || record.w < 0.5) return 1.0;

    uint selected = first;
    if (type < 1.5 && count > 1u) {
        float best = -2.0;
        for (uint index = 0u; index < count; ++index) {
            uint candidate = first + index;
            float3 origin = ShadowView(candidate, 0u).xyz;
            float3 direction = normalize(position - origin);
            float score = dot(direction, ShadowView(candidate, 1u).xyz);
            if (score > best) { best = score; selected = candidate; }
        }
    } else if (type > 1.5 && type < 2.5 && count > 1u) {
        float camera_depth = dot(position - PCameraPositionNear.xyz, PCameraForwardFar.xyz);
        selected = first + count - 1u;
        for (uint index = 0u; index < count; ++index) {
            uint candidate = first + index;
            if (camera_depth <= ShadowView(candidate, 4u).z) {
                selected = candidate;
                break;
            }
        }
    }

    return SampleShadow(selected, position, max(record.z, bias));
}

float3 DirectLighting(
    float3 position,
    float3 normal,
    float3 view,
    float3 albedo,
    float roughness,
    float metallic,
    GpuMaterial material,
    out float3 clearcoat_direct)
{
    float3 direct = 0.0.xxx;
    clearcoat_direct = 0.0.xxx;
    float spec_power = max(2.0 / max(roughness * roughness, 1.0e-3) - 2.0, 1.0);
    float3 f0 = lerp(0.04.xxx * material.specular.x * material.specular.yzw, albedo, metallic);
    float3 diffuse = albedo * (1.0 - metallic) / 3.14159265359;
    float clearcoat = material.clearcoat_sheen.x;
    float coat_roughness = max(material.clearcoat_sheen.y, 0.04);
    float coat_power = max(2.0 / max(coat_roughness * coat_roughness, 1.0e-3) - 2.0, 1.0);
    uint light_count = (uint)max(PGI[11].z, 0.0);
    uint light_base = (uint)max(PGI[11].w, 12.0);

    for (uint light_index = 0u; light_index < light_count; ++light_index) {
        uint offset = light_base + light_index * 5u;
        float4 position_intensity = PGI[offset + 0u];
        float4 direction_type = PGI[offset + 1u];
        float4 color_range = PGI[offset + 2u];
        float4 cone_shadow = PGI[offset + 3u];
        if (position_intensity.w <= 0.0) continue;

        float3 light_direction = normalize(direction_type.xyz);
        float3 l = -light_direction;
        float attenuation = 1.0;
        if (direction_type.w < 1.5 || direction_type.w > 2.5) {
            float3 to_light = position_intensity.xyz - position;
            float dist = length(to_light);
            if (dist <= 1.0e-5) continue;
            l = to_light / dist;
            attenuation = 1.0 / max(dist * dist, 1.0);
            if (color_range.w > 0.0) attenuation *= saturate(1.0 - dist / color_range.w);
            if (direction_type.w > 2.5)
                attenuation *= smoothstep(cone_shadow.y, cone_shadow.x, dot(-l, light_direction));
        }
        if (attenuation <= 0.0) continue;

        float ndotl = saturate(dot(normal, l));
        if (ndotl <= 0.0) continue;
        float shadow = 1.0;
        if (cone_shadow.z > 0.5) {
            float bias = max(cone_shadow.w, 1.0e-4);
            shadow = ShadowVisibility(
                light_index,
                position + normal * bias,
                direction_type.w,
                bias
            );
        }
        if (shadow <= 0.0) continue;

        float3 h = normalize(l + view);
        float ndoth = saturate(dot(normal, h));
        float spec_term = pow(ndoth, spec_power) * (spec_power + 2.0) / 8.0;
        float energy = position_intensity.w * attenuation * shadow;
        direct += (diffuse * ndotl + f0 * spec_term * ndotl) * color_range.rgb * energy;
        float coat_term = clearcoat * 0.04 * pow(ndoth, coat_power) * ndotl;
        clearcoat_direct += color_range.rgb * coat_term * energy;
    }
    return direct;
}

struct PSOut { float4 color : SV_Target0; float2 velocity : SV_Target1; };

PSOut PSMain(VSOut i) {
    PSOut o;
    uint mi = min(i.material, (uint)max(PCounts.z - 1, 0));
    GpuBaseMaterial base = PBaseMaterials[mi];
    GpuMaterial material = PMaterials[mi];
    float4 base_sample = material.tex0.x >= 0 ? SampleColorSlot(material.tex0.x, i.uv) : 1.0.xxxx;
    float alpha = base.base_color.a * base_sample.a;
    if (material.tex1.z >= 0) alpha *= SampleSlot(material.tex1.z, i.uv).r;
    if (material.misc.w > 0.5 && material.misc.w < 1.5 && alpha < material.misc.x) discard;

    float3 albedo = max(base.base_color.rgb * base_sample.rgb, 0.0.xxx);
    float roughness = saturate(material.pbr.x * (material.tex0.z >= 0 ? SampleSlot(material.tex0.z, i.uv).r : 1.0));
    roughness = max(roughness, 0.04);
    float metallic = saturate(material.pbr.y * (material.tex0.w >= 0 ? SampleSlot(material.tex0.w, i.uv).r : 1.0));
    float ao = saturate(material.pbr.z * (material.tex1.x >= 0 ? SampleSlot(material.tex1.x, i.uv).r : 1.0));
    float3 n = ApplyNormalMap(i.normal, i.world, i.uv, material.tex0.y, material.pbr.w);
    float3 v = normalize(PCameraPositionNear.xyz - i.world);

    float3 emissive = material.emissive_strength.rgb * material.emissive_strength.w;
    if (material.tex1.y >= 0) emissive *= SampleColorSlot(material.tex1.y, i.uv).rgb;
    if (material.misc.y > 0.5) {
        o.color = float4(albedo + emissive, alpha);
        o.velocity = 0.0.xx;
        return o;
    }

    float3 clearcoat_direct;
    float3 direct = DirectLighting(i.world, n, v, albedo, roughness, metallic, material, clearcoat_direct);
    float3 gi = SampleGI(i.world, n) * albedo * (1.0 - metallic);
    float3 ambient = PGI[6].xyz * PGI[6].w * albedo * ao;
    float3 sheen = material.sheen_thickness.rgb * pow(1.0 - saturate(dot(n,v)), 5.0) * (1.0 - material.clearcoat_sheen.z);
    float3 f0 = lerp(0.04.xxx * material.specular.x * material.specular.yzw, albedo, metallic);
    float iri = material.anisotropy_iridescence.z;
    float3 iridescent = iri * float3(0.5 + 0.5*sin(dot(v,n)*8.0), 0.5 + 0.5*sin(dot(v,n)*8.0+2.1), 0.5 + 0.5*sin(dot(v,n)*8.0+4.2));
    float transmission = saturate(material.clearcoat_sheen.w);
    float3 transmitted = EnvironmentColor(-v) * transmission * albedo;
    float3 color = direct + gi + ambient + emissive + sheen + clearcoat_direct + iridescent * f0 + transmitted;

    float distance_to_camera = length(i.world - PCameraPositionNear.xyz);
    float fog_factor = 0.0;
    if (PGI[5].w > 0.5 && PGI[5].w < 1.5)
        fog_factor = saturate((distance_to_camera - PGI[7].y) / max(PGI[7].z - PGI[7].y, 1.0e-5));
    else if (PGI[5].w > 1.5)
        fog_factor = 1.0 - exp(-max(PGI[7].x,0.0) * distance_to_camera);
    color = lerp(color, PGI[5].xyz, saturate(fog_factor));

    o.color = float4(max(color, 0.0.xxx), alpha);
    o.velocity = 0.0.xx;
    return o;
}
)HLSL";
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

inline constexpr const char *Trace = R"HLSL(
struct GpuNode { float3 minimum; uint first; float3 maximum; uint meta; uint4 extra; };
struct GpuTriangle {
    float4 p0; float4 p1; float4 p2;
    float4 n0; float4 n1; float4 n2;
    float4 uv01; float4 uv2;
};
struct GpuBaseMaterial { float4 base_color; int4 data; };
struct GpuMaterial {
    float4 emissive_strength; float4 pbr; float4 specular; float4 clearcoat_sheen;
    float4 sheen_thickness; float4 attenuation; float4 diffuse_transmission;
    float4 anisotropy_iridescence; float4 iridescence_dispersion_ior; float4 misc;
    int4 tex0; int4 tex1; int4 tex2; int4 tex3; int4 tex4; int4 tex5;
};

Texture2D<float4> Tex0  : register(t0,  space0); SamplerState Samp0  : register(s0,  space0);
Texture2D<float4> Tex1  : register(t1,  space0); SamplerState Samp1  : register(s1,  space0);
Texture2D<float4> Tex2  : register(t2,  space0); SamplerState Samp2  : register(s2,  space0);
Texture2D<float4> Tex3  : register(t3,  space0); SamplerState Samp3  : register(s3,  space0);
Texture2D<float4> Tex4  : register(t4,  space0); SamplerState Samp4  : register(s4,  space0);
Texture2D<float4> Tex5  : register(t5,  space0); SamplerState Samp5  : register(s5,  space0);
Texture2D<float4> Tex6  : register(t6,  space0); SamplerState Samp6  : register(s6,  space0);
Texture2D<float4> Tex7  : register(t7,  space0); SamplerState Samp7  : register(s7,  space0);
Texture2D<float4> Tex8  : register(t8,  space0); SamplerState Samp8  : register(s8,  space0);
Texture2D<float4> Tex9  : register(t9,  space0); SamplerState Samp9  : register(s9,  space0);
Texture2D<float4> Tex10 : register(t10, space0); SamplerState Samp10 : register(s10, space0);
Texture2D<float4> Tex11 : register(t11, space0); SamplerState Samp11 : register(s11, space0);
Texture2D<float4> Tex12 : register(t12, space0); SamplerState Samp12 : register(s12, space0);
Texture2D<float4> Tex13 : register(t13, space0); SamplerState Samp13 : register(s13, space0);
Texture2D<float4> Tex14 : register(t14, space0); SamplerState Samp14 : register(s14, space0);
Texture2D<float4> Tex15 : register(t15, space0); SamplerState Samp15 : register(s15, space0);
StructuredBuffer<GpuNode> Nodes : register(t16, space0);
StructuredBuffer<GpuTriangle> Triangles : register(t17, space0);
StructuredBuffer<GpuBaseMaterial> BaseMaterials : register(t18, space0);
StructuredBuffer<GpuMaterial> Materials : register(t19, space0);
StructuredBuffer<float4> GI : register(t20, space0);

RWTexture2D<float4> Output : register(u0, space1);
RWTexture2D<float> LinearDepth : register(u1, space1);
RWTexture2D<float4> Accumulation : register(u2, space1);

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

float4 SampleSlot(int slot, float2 uv) {
    if (slot == 0) return Tex0.SampleLevel(Samp0,uv,0);
    if (slot == 1) return Tex1.SampleLevel(Samp1,uv,0);
    if (slot == 2) return Tex2.SampleLevel(Samp2,uv,0);
    if (slot == 3) return Tex3.SampleLevel(Samp3,uv,0);
    if (slot == 4) return Tex4.SampleLevel(Samp4,uv,0);
    if (slot == 5) return Tex5.SampleLevel(Samp5,uv,0);
    if (slot == 6) return Tex6.SampleLevel(Samp6,uv,0);
    if (slot == 7) return Tex7.SampleLevel(Samp7,uv,0);
    if (slot == 8) return Tex8.SampleLevel(Samp8,uv,0);
    if (slot == 9) return Tex9.SampleLevel(Samp9,uv,0);
    if (slot == 10) return Tex10.SampleLevel(Samp10,uv,0);
    if (slot == 11) return Tex11.SampleLevel(Samp11,uv,0);
    if (slot == 12) return Tex12.SampleLevel(Samp12,uv,0);
    if (slot == 13) return Tex13.SampleLevel(Samp13,uv,0);
    if (slot == 14) return Tex14.SampleLevel(Samp14,uv,0);
    if (slot == 15) return Tex15.SampleLevel(Samp15,uv,0);
    return 1.0.xxxx;
}

float3 SrgbToLinear(float3 color) {
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(low, high, step(0.04045.xxx, color));
}

float4 SampleColorSlot(int slot, float2 uv) {
    float4 sample = SampleSlot(slot, uv);
    sample.rgb = SrgbToLinear(sample.rgb);
    return sample;
}

uint Hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
float Random(inout uint state) { state=Hash(state); return (state & 0x00ffffffu) / 16777216.0; }

bool IntersectAabb(float3 ro,float3 inv_rd,float3 bmin,float3 bmax,float max_t) {
    float3 t0=(bmin-ro)*inv_rd, t1=(bmax-ro)*inv_rd;
    float3 mn=min(t0,t1), mx=max(t0,t1);
    float enter=max(max(mn.x,mn.y),max(mn.z,0.0));
    float leave=min(min(mx.x,mx.y),mx.z);
    return leave>=enter && enter<max_t;
}

bool IntersectTriangle(float3 ro,float3 rd,GpuTriangle tri,out float t,out float u,out float v) {
    t=0.0f;u=0.0f;v=0.0f;
    float3 e1=tri.p1.xyz-tri.p0.xyz, e2=tri.p2.xyz-tri.p0.xyz;
    float3 p=cross(rd,e2); float det=dot(e1,p);
    if(abs(det)<1e-7) return false;
    float inv=1.0/det; float3 s=ro-tri.p0.xyz;
    u=dot(s,p)*inv; if(u<0||u>1) return false;
    float3 q=cross(s,e1); v=dot(rd,q)*inv; if(v<0||u+v>1) return false;
    t=dot(e2,q)*inv; return t>1e-4;
}

struct Hit { bool hit; float t; uint triangle_index; float2 bary; };
Hit TraceClosest(float3 ro,float3 rd,float max_t) {
    Hit result; result.hit=false; result.t=max_t; result.triangle_index=0; result.bary=0;
    if(Counts.x<=0||Counts.y<=0) return result;
    float3 safe=rd; safe = sign(safe) * max(abs(safe), 1e-8.xxx);
    float3 inv_rd=1.0/safe;
    uint stack[64]; uint top=0; stack[top++]=0;
    while(top>0){
        uint ni=stack[--top]; if(ni>=(uint)Counts.x) continue;
        GpuNode node=Nodes[ni];
        if(!IntersectAabb(ro,inv_rd,node.minimum,node.maximum,result.t)) continue;
        if((node.meta&0x80000000u)!=0u){
            uint count=node.meta&0x7fffffffu;
            for(uint i=0;i<count;i++){
                uint ti=node.first+i; if(ti>=(uint)Counts.y) continue;
                float t,u,v;
                if(IntersectTriangle(ro,rd,Triangles[ti],t,u,v)&&t<result.t){result.hit=true;result.t=t;result.triangle_index=ti;result.bary=float2(u,v);}
            }
        } else if(top+2u<=64u){stack[top++]=node.first;stack[top++]=node.meta;}
    }
    return result;
}

bool Occluded(float3 ro,float3 rd,float max_t){Hit h=TraceClosest(ro,rd,max_t);return h.hit&&h.t<max_t;}

float3 EnvironmentColor(float3 direction){
    float3 sky=GI[4].xyz; float intensity=max(GI[3].w,0.0);
    if(GI[4].w>0.5){const float PI=3.14159265359;float u=frac(atan2(direction.z,direction.x)/(2*PI)+0.5+GI[7].w/(2*PI));float v=acos(clamp(direction.y,-1.0,1.0))/PI;return SampleColorSlot(0,float2(u,v)).rgb*intensity;}
    return sky*max(intensity,1.0);
}

float3 SampleGI(float3 position,float3 normal){
    if(GI[0].w<0.5||GI[2].w<1.0)return 0.0.xxx;
    float3 minimum=GI[0].xyz,maximum=GI[1].xyz;float intensity=max(GI[1].w,0.0);uint3 size=(uint3)max(GI[2].xyz,1.0.xxx);
    float3 g=saturate((position-minimum)/max(maximum-minimum,1e-6.xxx))*(float3(size)-1.0);uint3 p0=(uint3)floor(g);uint3 p1=min(p0+1u,size-1u);float3 f=g-float3(p0);float3 c[4];
    [unroll]for(uint k=0;k<4;k++){uint i000=p0.x+size.x*(p0.y+size.y*p0.z),i100=p1.x+size.x*(p0.y+size.y*p0.z),i010=p0.x+size.x*(p1.y+size.y*p0.z),i110=p1.x+size.x*(p1.y+size.y*p0.z),i001=p0.x+size.x*(p0.y+size.y*p1.z),i101=p1.x+size.x*(p0.y+size.y*p1.z),i011=p0.x+size.x*(p1.y+size.y*p1.z),i111=p1.x+size.x*(p1.y+size.y*p1.z);float3 a=lerp(GI[12+i000*4+k].xyz,GI[12+i100*4+k].xyz,f.x),b=lerp(GI[12+i010*4+k].xyz,GI[12+i110*4+k].xyz,f.x),d=lerp(GI[12+i001*4+k].xyz,GI[12+i101*4+k].xyz,f.x),e=lerp(GI[12+i011*4+k].xyz,GI[12+i111*4+k].xyz,f.x);c[k]=lerp(lerp(a,b,f.y),lerp(d,e,f.y),f.z);}
    float3 n=normalize(normal);const float PI=3.14159265359,Y00=.28209479177,Y1=.48860251190;float3 r=c[0]*(PI*Y00)+c[1]*((2*PI/3)*Y1*n.x)+c[2]*((2*PI/3)*Y1*n.y)+c[3]*((2*PI/3)*Y1*n.z);return max(r,0.0.xxx)*intensity;
}

void CameraRay(float2 pixel,float2 jitter,out float3 origin,out float3 direction){
    float2 ndc=((pixel+jitter)/Resolution.xy)*2.0-1.0;
    ndc.y=-ndc.y;
    if(ProjectionAlpha.z>0.5){origin=CameraPositionNear.xyz+CameraRightAspect.xyz*ndc.x*ProjectionAlpha.x+CameraUpTanHalfFov.xyz*ndc.y*ProjectionAlpha.y;direction=normalize(CameraForwardFar.xyz);}
    else{origin=CameraPositionNear.xyz;direction=normalize(CameraForwardFar.xyz+CameraRightAspect.xyz*ndc.x*CameraUpTanHalfFov.w*CameraRightAspect.w+CameraUpTanHalfFov.xyz*ndc.y*CameraUpTanHalfFov.w);}
}

struct Surface { float3 position; float3 normal; float2 uv; uint material; float3 albedo; float alpha; float roughness; float metallic; float ao; float3 emission; };
Surface MakeSurface(Hit hit,float3 rd){
    GpuTriangle tri=Triangles[hit.triangle_index];float u=hit.bary.x,v=hit.bary.y,w=1-u-v;Surface s;s.position=tri.p0.xyz*w+tri.p1.xyz*u+tri.p2.xyz*v;s.normal=normalize(tri.n0.xyz*w+tri.n1.xyz*u+tri.n2.xyz*v);if(dot(s.normal,rd)>0)s.normal=-s.normal;s.uv=tri.uv01.xy*w+tri.uv01.zw*u+tri.uv2.xy*v;s.material=asuint(tri.p0.w);uint mi=min(s.material,(uint)max(Counts.z-1,0));GpuBaseMaterial b=BaseMaterials[mi];GpuMaterial m=Materials[mi];float4 base=m.tex0.x>=0?SampleColorSlot(m.tex0.x,s.uv):1.0.xxxx;s.albedo=max(b.base_color.rgb*base.rgb,0.0.xxx);s.alpha=b.base_color.a*base.a;if(m.tex1.z>=0)s.alpha*=SampleSlot(m.tex1.z,s.uv).r;s.roughness=max(saturate(m.pbr.x*(m.tex0.z>=0?SampleSlot(m.tex0.z,s.uv).r:1)),.04);s.metallic=saturate(m.pbr.y*(m.tex0.w>=0?SampleSlot(m.tex0.w,s.uv).r:1));s.ao=saturate(m.pbr.z*(m.tex1.x>=0?SampleSlot(m.tex1.x,s.uv).r:1));s.emission=m.emissive_strength.rgb*m.emissive_strength.w;if(m.tex1.y>=0)s.emission*=SampleColorSlot(m.tex1.y,s.uv).rgb;
    if(m.tex0.y>=0){float3 map=SampleSlot(m.tex0.y,s.uv).xyz*2-1;map.xy*=m.pbr.w;float3 e1=tri.p1.xyz-tri.p0.xyz,e2=tri.p2.xyz-tri.p0.xyz;float2 d1=tri.uv01.zw-tri.uv01.xy,d2=tri.uv2.xy-tri.uv01.xy;float det=d1.x*d2.y-d1.y*d2.x;if(abs(det)>1e-6){float inv=1/det;float3 t=normalize((e1*d2.y-e2*d1.y)*inv);float3 bt=normalize((-e1*d2.x+e2*d1.x)*inv);s.normal=normalize(t*map.x+bt*map.y+s.normal*map.z);}}
    return s;
}

float3 DirectLight(Surface s,float3 view){
    GpuMaterial m=Materials[min(s.material,(uint)max(Counts.z-1,0))];
    float p=max(2/max(s.roughness*s.roughness,.001)-2,1);
    float3 f0=lerp(.04.xxx*m.specular.x*m.specular.yzw,s.albedo,s.metallic);
    float3 diffuse=s.albedo*(1-s.metallic)/3.14159265359;
    float3 result=0.0.xxx;
    uint light_count=(uint)max(GI[11].z,0.0);
    uint light_base=(uint)max(GI[11].w,12.0);
    for(uint light_index=0u;light_index<light_count;++light_index){
        uint offset=light_base+light_index*5u;
        float4 position_intensity=GI[offset+0u];
        float4 direction_type=GI[offset+1u];
        float4 color_range=GI[offset+2u];
        float4 cone_shadow=GI[offset+3u];
        if(position_intensity.w<=0.0)continue;
        float3 light_direction=normalize(direction_type.xyz);
        float3 l=-light_direction;
        float attenuation=1.0,max_t=1e30;
        if(direction_type.w<1.5||direction_type.w>2.5){
            float3 d=position_intensity.xyz-s.position;float dist=length(d);if(dist<=1e-5)continue;
            l=d/dist;float bias=max(cone_shadow.w,1e-4);max_t=max(dist-bias,0.0);attenuation=1/max(dist*dist,1.0);
            if(color_range.w>0)attenuation*=saturate(1-dist/color_range.w);
            if(direction_type.w>2.5)attenuation*=smoothstep(cone_shadow.y,cone_shadow.x,dot(-l,light_direction));
        }
        float ndl=saturate(dot(s.normal,l));if(ndl<=0||attenuation<=0)continue;
        if(cone_shadow.z>0.5&&max_t>0){float bias=max(cone_shadow.w,1e-4);if(Occluded(s.position+s.normal*bias,l,max_t))continue;}
        float3 h=normalize(l+view);float spec=pow(saturate(dot(s.normal,h)),p)*(p+2)/8;
        result+=(diffuse*ndl+f0*spec*ndl)*color_range.xyz*position_intensity.w*attenuation;
    }
    return result;
}

float3 ShadeRay(float3 origin,float3 direction,out float depth){
    Hit hit=TraceClosest(origin,direction,1e30);if(!hit.hit){depth=0;return EnvironmentColor(direction);}depth=hit.t;Surface s=MakeSurface(hit,direction);GpuMaterial m=Materials[min(s.material,(uint)max(Counts.z-1,0))];if(m.misc.w>0.5&&m.misc.w<1.5&&s.alpha<m.misc.x){Hit second=TraceClosest(s.position+direction*.002,direction,1e30);if(!second.hit)return EnvironmentColor(direction);s=MakeSurface(second,direction);depth+=second.t;}if(m.misc.y>0.5)return s.albedo+s.emission;float3 direct=DirectLight(s,normalize(origin-s.position));float3 indirect=SampleGI(s.position,s.normal)*s.albedo*(1-s.metallic)+GI[6].xyz*GI[6].w*s.albedo*s.ao;return max(direct+indirect+s.emission,0.0.xxx);
}

float3 CosineHemisphere(float3 n,inout uint rng){float r1=Random(rng),r2=Random(rng);float phi=6.28318530718*r1;float r=sqrt(r2);float3 t=normalize(abs(n.z)<.999?cross(float3(0,0,1),n):cross(float3(0,1,0),n));float3 b=cross(n,t);return normalize(t*(cos(phi)*r)+b*(sin(phi)*r)+n*sqrt(max(1-r2,0.0)));}

[numthreads(8,8,1)]
void RayMain(uint3 tid:SV_DispatchThreadID){if(tid.x>=(uint)Resolution.x||tid.y>=(uint)Resolution.y)return;float3 ro,rd;CameraRay(float2(tid.xy),float2(.5,.5),ro,rd);float depth;float3 color=ShadeRay(ro,rd,depth);Output[tid.xy]=float4(color,1);LinearDepth[tid.xy]=depth;Accumulation[tid.xy]=float4(color,1);}

[numthreads(8,8,1)]
void PathMain(uint3 tid:SV_DispatchThreadID){
    uint width=(uint)Resolution.x,height=(uint)Resolution.y;
    if(tid.x>=width||tid.y>=height)return;
    bool reset=(Frame.w&1u)!=0u;
    bool moving=(Frame.w&2u)!=0u;
    if(reset)Accumulation[tid.xy]=0.0.xxxx;

    if(moving){
        uint block=max(PathPolicy.w,1u);
        if((tid.x%block)==0u&&(tid.y%block)==0u){
            float2 sample_pixel=min(float2(tid.xy)+float2(block*.5f,block*.5f),float2(width,height)-.5.xx);
            float3 dro,drd;CameraRay(sample_pixel,0.0.xx,dro,drd);Hit dh=TraceClosest(dro,drd,1e30);
            float depth_value=dh.hit?dh.t:0.0f;
            uint2 end=min(tid.xy+uint2(block,block),uint2(width,height));
            for(uint y=tid.y;y<end.y;++y)for(uint x=tid.x;x<end.x;++x)LinearDepth[uint2(x,y)]=depth_value;
        }
    }else if(reset){
        float3 dro,drd;CameraRay(float2(tid.xy),.5.xx,dro,drd);Hit dh=TraceClosest(dro,drd,1e30);LinearDepth[tid.xy]=dh.hit?dh.t:0.0f;
    }

    uint phase_grid=moving?max(PathPolicy.z,1u):(reset?max(PathPolicy.y,1u):max(PathPolicy.x,1u));
    uint phase_count=phase_grid*phase_grid;
    uint phase=Frame.x%max(phase_count,1u);
    uint pixel_phase=(tid.x%phase_grid)+(tid.y%phase_grid)*phase_grid;
    if(pixel_phase!=phase)return;

    uint rng=Hash(tid.x+tid.y*width+Frame.x*747796405u+1u);
    float3 sum=0.0.xxx;float first_depth=0;uint spp=max(Frame.z,1u);
    for(uint sample=0;sample<spp;sample++){
        float2 jitter=float2(Random(rng),Random(rng));float3 ro,rd;CameraRay(float2(tid.xy),jitter,ro,rd);
        float3 throughput=1.0.xxx;float3 radiance=0.0.xxx;
        for(uint bounce=0;bounce<4u;bounce++){
            Hit hit=TraceClosest(ro,rd,1e30);if(!hit.hit){radiance+=throughput*EnvironmentColor(rd);break;}
            if(bounce==0)first_depth=hit.t;Surface s=MakeSurface(hit,rd);GpuMaterial m=Materials[min(s.material,(uint)max(Counts.z-1,0))];
            if(m.misc.w>0.5&&m.misc.w<1.5&&s.alpha<m.misc.x){ro=s.position+rd*.002;continue;}
            radiance+=throughput*s.emission;if(m.misc.y>0.5){radiance+=throughput*s.albedo;break;}
            radiance+=throughput*DirectLight(s,-rd);radiance+=throughput*SampleGI(s.position,s.normal)*s.albedo*.25;
            float spec_probability=lerp(.08,.65,s.metallic)*(1.0-s.roughness*.5);
            if(Random(rng)<spec_probability){float3 reflected=reflect(rd,s.normal);rd=normalize(lerp(reflected,CosineHemisphere(s.normal,rng),s.roughness*s.roughness));throughput*=lerp(.04.xxx,s.albedo,s.metallic)/max(spec_probability,.05);}
            else{rd=CosineHemisphere(s.normal,rng);throughput*=s.albedo*(1-s.metallic)/max(1-spec_probability,.05);}
            ro=s.position+s.normal*.002;if(bounce>=2){float survive=saturate(max(throughput.x,max(throughput.y,throughput.z)));survive=max(survive,.1);if(Random(rng)>survive)break;throughput/=survive;}
        }
        sum+=radiance;
    }
    float4 previous=reset?0.0.xxxx:Accumulation[tid.xy];float4 accumulated=previous+float4(sum,spp);
    Accumulation[tid.xy]=accumulated;Output[tid.xy]=float4(accumulated.rgb/max(accumulated.a,1.0),1);
    if(!moving)LinearDepth[tid.xy]=first_depth;
}
)HLSL";

inline constexpr const char *PathResolve = R"HLSL(
Texture2D<float4> Accumulation : register(t0, space0);
SamplerState AccumulationSampler : register(s0, space0);
RWTexture2D<float4> Resolved : register(u0, space1);
cbuffer ResolveData : register(b0, space2) {
    uint4 Params;
};

float4 LoadSample(int2 pixel,int2 size){
    pixel=clamp(pixel,int2(0,0),size-int2(1,1));
    float2 uv=(float2(pixel)+.5.xx)/float2(size);
    return Accumulation.SampleLevel(AccumulationSampler,uv,0.0f);
}

float4 ReconstructSparse(int2 pixel,int2 size,int phase_grid,int phase){
    pixel=clamp(pixel,int2(0,0),size-int2(1,1));int grid=max(phase_grid,1);int current=clamp(phase,0,max(grid*grid-1,0));
    int2 offset=int2(current%grid,current/grid);int2 max_cell=max((size-int2(1,1)-offset)/grid,int2(0,0));
    float2 lattice=(float2(pixel)-float2(offset))/float(grid);float2 g=clamp(lattice,0.0.xx,float2(max_cell));
    int2 c0=int2(floor(g)),c1=min(c0+int2(1,1),max_cell);float2 t=g-float2(c0);
    int2 p00=c0*grid+offset,p10=int2(c1.x,c0.y)*grid+offset,p01=int2(c0.x,c1.y)*grid+offset,p11=c1*grid+offset;
    float4 s00=LoadSample(p00,size),s10=LoadSample(p10,size),s01=LoadSample(p01,size),s11=LoadSample(p11,size);
    float w00=(1-t.x)*(1-t.y)*(s00.a>0?1:0),w10=t.x*(1-t.y)*(s10.a>0?1:0),w01=(1-t.x)*t.y*(s01.a>0?1:0),w11=t.x*t.y*(s11.a>0?1:0);
    float sum=w00+w10+w01+w11;return sum<=1e-6?s00:(s00*w00+s10*w10+s01*w01+s11*w11)/sum;
}

[numthreads(8,8,1)]
void Main(uint3 tid:SV_DispatchThreadID){
    uint width=Params.x,height=Params.y;if(tid.x>=width||tid.y>=height)return;
    bool moving=(Params.z&1u)!=0u;uint grid=max(Params.w,1u);uint phase=Params.z>>1u;
    float4 value=moving?ReconstructSparse(int2(tid.xy),int2(width,height),int(grid),int(phase)):LoadSample(int2(tid.xy),int2(width,height));
    Resolved[tid.xy]=float4(max(value.rgb/max(value.a,1.0f),0.0.xxx),1.0f);
}
)HLSL";

} // namespace Renderer::SDLGPU::Shaders

#endif