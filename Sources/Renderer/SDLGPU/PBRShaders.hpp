#ifndef HORSE_RENDERER_SDLGPU_PBR_SHADERS_HPP
#define HORSE_RENDERER_SDLGPU_PBR_SHADERS_HPP

namespace Renderer::SDLGPU::PBRShaders {

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
cbuffer VertexDraw : register(b1, space1) { uint4 VDraw; };

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
Texture2D<float4> AmbientOcclusion : register(t16, space2);
SamplerState AmbientOcclusionSampler : register(s16, space2);
StructuredBuffer<GpuBaseMaterial> PBaseMaterials : register(t17, space2);
StructuredBuffer<GpuMaterial> PMaterials : register(t18, space2);
StructuredBuffer<float4> PGI : register(t19, space2);
StructuredBuffer<float4> PShadows : register(t20, space2);
StructuredBuffer<uint> PForwardTileCounts : register(t21, space2);
StructuredBuffer<uint> PForwardLightIndices : register(t22, space2);
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
cbuffer PixelForward : register(b1, space3) { uint4 PForward; };
cbuffer PixelAmbientOcclusion : register(b2, space3) { uint4 PAmbientOcclusion; };

static const float PI = 3.14159265359;

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
    color = saturate(color);
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, 2.4);
    return lerp(low, high, step(0.04045.xxx, color));
}

float4 SampleColorSlot(int slot, float2 uv) {
    float4 value = SampleSlot(slot, uv);
    value.rgb = SrgbToLinear(value.rgb);
    return value;
}

float Max3(float3 value) { return max(value.x, max(value.y, value.z)); }
float Square(float value) { return value * value; }
float3 FresnelSchlick(float3 f0, float cos_theta) {
    float f = pow(1.0 - saturate(cos_theta), 5.0);
    return f0 + (1.0.xxx - f0) * f;
}
float FresnelSchlickScalar(float f0, float cos_theta) {
    float f = pow(1.0 - saturate(cos_theta), 5.0);
    return f0 + (1.0 - f0) * f;
}
float D_GGX(float no_h, float alpha) {
    float a2 = max(alpha * alpha, 1.0e-6);
    float d = no_h * no_h * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1.0e-6);
}
float V_GGX(float no_v, float no_l, float alpha) {
    float a2 = max(alpha * alpha, 1.0e-6);
    float gv = no_l * sqrt(max(no_v * no_v * (1.0 - a2) + a2, 0.0));
    float gl = no_v * sqrt(max(no_l * no_l * (1.0 - a2) + a2, 0.0));
    return 0.5 / max(gv + gl, 1.0e-6);
}
float D_GGX_Anisotropic(float no_h, float to_h, float bo_h, float ax, float ay) {
    float d = Square(to_h / max(ax, 1.0e-4)) +
        Square(bo_h / max(ay, 1.0e-4)) + no_h * no_h;
    return 1.0 / max(PI * ax * ay * d * d, 1.0e-6);
}
float V_GGX_Anisotropic(
    float no_v, float no_l,
    float to_v, float bo_v,
    float to_l, float bo_l,
    float ax, float ay)
{
    float lambda_v = no_l * length(float3(ax * to_v, ay * bo_v, no_v));
    float lambda_l = no_v * length(float3(ax * to_l, ay * bo_l, no_l));
    return 0.5 / max(lambda_v + lambda_l, 1.0e-6);
}
float D_Charlie(float no_h, float roughness) {
    float r = max(roughness, 0.02);
    float inv_r = 1.0 / r;
    float sin2 = max(1.0 - no_h * no_h, 1.0e-6);
    return (2.0 + inv_r) * pow(sin2, 0.5 * inv_r) / (2.0 * PI);
}
float V_Neubelt(float no_v, float no_l) {
    return 1.0 / max(4.0 * (no_l + no_v - no_l * no_v), 1.0e-5);
}

struct VSOut {
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint material : TEXCOORD3;
    nointerpolation uint mirrored : TEXCOORD4;
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
void RasterSkin(RasterVertex vertex, RasterItem item, out float3 position, out float3 normal) {
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
        o.mirrored = 0u;
        return o;
    }
    float3 local_position;
    float3 local_normal;
    RasterSkin(vertex, item, local_position, local_normal);
    float3 p = RasterTransformPoint(item.model0, item.model1, item.model2, item.model3, local_position);
    float3 n = RasterWorldNormal(item, local_normal);
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
    o.uv = vertex.uv.xy;
    o.material = item.meta.x;
    o.mirrored = item.flags.y;
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
        float3 a = lerp(PGI[12u+i000*4u+c].xyz, PGI[12u+i100*4u+c].xyz, f.x);
        float3 b = lerp(PGI[12u+i010*4u+c].xyz, PGI[12u+i110*4u+c].xyz, f.x);
        float3 d = lerp(PGI[12u+i001*4u+c].xyz, PGI[12u+i101*4u+c].xyz, f.x);
        float3 e = lerp(PGI[12u+i011*4u+c].xyz, PGI[12u+i111*4u+c].xyz, f.x);
        coeff[c] = lerp(lerp(a,b,f.y), lerp(d,e,f.y), f.z);
    }
    float3 n = normalize(normal);
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
        float rotation = PGI[7].w;
        float u = frac(atan2(direction.z, direction.x) / (2.0*PI) + 0.5 + rotation/(2.0*PI));
        float v = acos(clamp(direction.y, -1.0, 1.0)) / PI;
        return SampleColorSlot(0, float2(u,v)).rgb * intensity;
    }
    return sky * max(intensity, 1.0);
}

void BuildBasis(float3 n, float3 position, float2 uv, out float3 tangent, out float3 bitangent) {
    float3 dp1 = ddx(position);
    float3 dp2 = ddy(position);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) > 1.0e-8) {
        tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) / det);
        tangent = normalize(tangent - n * dot(n, tangent));
        bitangent = normalize(cross(n, tangent)) * (det < 0.0 ? -1.0 : 1.0);
        if (all(isfinite(tangent)) && all(isfinite(bitangent))) return;
    }
    float3 up = abs(n.z) < 0.999 ? float3(0,0,1) : float3(0,1,0);
    tangent = normalize(cross(up, n));
    bitangent = normalize(cross(n, tangent));
}

float3 ApplyNormal(float3 n, float3 tangent, float3 bitangent, int slot, float2 uv, float scale) {
    if (slot < 0) return normalize(n);
    float3 mapped = SampleSlot(slot, uv).xyz * 2.0 - 1.0;
    mapped.xy *= scale;
    return normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);
}

struct Surface {
    float3 albedo;
    float alpha;
    float roughness;
    float metallic;
    float ao;
    float3 normal;
    float3 tangent;
    float3 bitangent;
    float3 coat_normal;
    float3 emission;
    float specular_factor;
    float3 specular_color;
    float clearcoat;
    float clearcoat_roughness;
    float3 sheen_color;
    float sheen_roughness;
    float transmission;
    float thickness;
    float3 attenuation_color;
    float attenuation_distance;
    float anisotropy;
    float iridescence;
    float iridescence_ior;
    float iridescence_thickness;
    float dispersion;
    float ior;
    float diffuse_transmission;
    float3 diffuse_transmission_color;
};

Surface EvaluateSurface(VSOut i, GpuBaseMaterial base, GpuMaterial material, bool front_face) {
    Surface s;
    float4 base_sample = material.tex0.x >= 0 ? SampleColorSlot(material.tex0.x, i.uv) : 1.0.xxxx;
    s.alpha = base.base_color.a * base_sample.a;
    if (material.tex1.z >= 0) s.alpha *= SampleSlot(material.tex1.z, i.uv).r;
    s.albedo = max(base.base_color.rgb * base_sample.rgb, 0.0.xxx);

    float4 mr = material.tex0.z >= 0 ? SampleSlot(material.tex0.z, i.uv) : 1.0.xxxx;
    bool gltf_mr = material.tex5.z > 0;
    s.roughness = max(saturate(material.pbr.x * (gltf_mr ? mr.g : mr.r)), 0.04);
    s.metallic = saturate(material.pbr.y * (gltf_mr ? mr.b : mr.g));
    s.ao = saturate(material.pbr.z * (material.tex1.x >= 0 ? SampleSlot(material.tex1.x, i.uv).r : 1.0));

    float3 geometric = normalize(i.normal) * (front_face ? 1.0 : -1.0);
    float3 base_tangent, base_bitangent;
    BuildBasis(geometric, i.world, i.uv, base_tangent, base_bitangent);
    s.normal = ApplyNormal(geometric, base_tangent, base_bitangent, material.tex0.y, i.uv, material.pbr.w);
    s.coat_normal = ApplyNormal(
        geometric,
        base_tangent,
        base_bitangent,
        material.tex2.y,
        i.uv,
        max(asfloat(material.tex5.y), 0.0));

    float4 anisotropy_sample = material.tex4.z >= 0 ? SampleSlot(material.tex4.z, i.uv) : float4(0.5,0.5,1,1);
    s.anisotropy = saturate(material.anisotropy_iridescence.x * anisotropy_sample.b);
    float2 direction = material.tex4.z >= 0 ? anisotropy_sample.rg * 2.0 - 1.0 : float2(1,0);
    if (dot(direction, direction) < 1.0e-6) direction = float2(1,0);
    direction = normalize(direction);
    float angle = material.anisotropy_iridescence.y;
    float sn = sin(angle), cs = cos(angle);
    direction = float2(direction.x*cs-direction.y*sn, direction.x*sn+direction.y*cs);
    float3 tangent = normalize(base_tangent * direction.x + base_bitangent * direction.y);
    tangent = normalize(tangent - s.normal * dot(s.normal, tangent));
    s.tangent = tangent;
    s.bitangent = normalize(cross(s.normal, tangent));

    s.emission = material.emissive_strength.rgb * material.emissive_strength.w;
    if (material.tex1.y >= 0) s.emission *= SampleColorSlot(material.tex1.y, i.uv).rgb;

    float4 coat = material.tex1.w >= 0 ? SampleSlot(material.tex1.w, i.uv) : 1.0.xxxx;
    s.clearcoat = saturate(material.clearcoat_sheen.x * coat.r);
    s.clearcoat_roughness = max(saturate(material.clearcoat_sheen.y * coat.g), 0.04);

    float4 sheen = material.tex2.z >= 0 ? SampleSlot(material.tex2.z, i.uv) : 1.0.xxxx;
    s.sheen_color = max(material.sheen_thickness.rgb *
        (material.tex2.z >= 0 ? SrgbToLinear(sheen.rgb) : 1.0.xxx), 0.0.xxx);
    s.sheen_roughness = max(saturate(material.clearcoat_sheen.z * sheen.a), 0.02);

    float4 transmission_thickness = material.tex3.x >= 0 ? SampleSlot(material.tex3.x, i.uv) : 1.0.xxxx;
    s.transmission = saturate(material.clearcoat_sheen.w * transmission_thickness.r);
    s.thickness = max(material.sheen_thickness.w * transmission_thickness.g, 0.0);
    s.attenuation_color = max(material.attenuation.rgb, 1.0e-4.xxx);
    s.attenuation_distance = max(material.attenuation.w, 0.0);

    float4 specular = material.tex3.z >= 0 ? SampleSlot(material.tex3.z, i.uv) : 1.0.xxxx;
    s.specular_factor = saturate(material.specular.x * specular.a);
    s.specular_color = max(material.specular.yzw *
        (material.tex3.z >= 0 ? SrgbToLinear(specular.rgb) : 1.0.xxx), 0.0.xxx);

    float4 iri = material.tex4.x >= 0 ? SampleSlot(material.tex4.x, i.uv) : 1.0.xxxx;
    s.iridescence = saturate(material.anisotropy_iridescence.z * iri.r);
    s.iridescence_ior = max(material.anisotropy_iridescence.w, 1.0);
    s.iridescence_thickness = material.tex4.x >= 0
        ? lerp(material.iridescence_dispersion_ior.x, material.iridescence_dispersion_ior.y, iri.g)
        : material.iridescence_dispersion_ior.y;
    s.dispersion = max(material.iridescence_dispersion_ior.z, 0.0);
    s.ior = max(material.iridescence_dispersion_ior.w, 1.0001);
    float4 diffuse_transmission = material.tex4.w >= 0 ? SampleSlot(material.tex4.w, i.uv) : 1.0.xxxx;
    s.diffuse_transmission = saturate(material.diffuse_transmission.w * diffuse_transmission.a);
    s.diffuse_transmission_color = max(material.diffuse_transmission.rgb *
        (material.tex4.w >= 0 ? SrgbToLinear(diffuse_transmission.rgb) : 1.0.xxx), 0.0.xxx);
    return s;
}

float3 SurfaceF0(Surface s) {
    float dielectric = Square((s.ior - 1.0) / (s.ior + 1.0));
    float3 dielectric_f0 = saturate(dielectric * s.specular_factor * s.specular_color);
    return lerp(dielectric_f0, s.albedo, s.metallic);
}

float3 IridescentFresnel(float3 fresnel, float vo_h, Surface s) {
    if (s.iridescence <= 0.0 || s.iridescence_thickness <= 0.0) return fresnel;
    float eta = max(s.iridescence_ior, 1.0);
    float sin2 = max(1.0 - vo_h * vo_h, 0.0) / (eta * eta);
    float cos_film = sqrt(max(1.0 - sin2, 0.0));
    float3 wavelength = float3(650.0, 510.0, 475.0);
    float3 phase = 4.0 * PI * eta * s.iridescence_thickness * cos_film / wavelength;
    float3 interference = 0.5.xxx + 0.5 * cos(phase);
    float interface_f0 = Square((eta - 1.0) / (eta + 1.0));
    float3 film = saturate(interface_f0.xxx +
        (1.0.xxx - interface_f0.xxx) * interference * 0.65);
    return lerp(fresnel, saturate(fresnel + (1.0.xxx - fresnel) * film), s.iridescence);
}

float3 VolumeAttenuation(Surface s) {
    if (s.thickness <= 0.0 || s.attenuation_distance <= 0.0) return 1.0.xxx;
    return pow(s.attenuation_color, s.thickness / max(s.attenuation_distance, 1.0e-5));
}

float3 SpecularBRDF(Surface s, float3 v, float3 l, float3 h, out float3 fresnel) {
    float no_v = max(dot(s.normal, v), 1.0e-4);
    float no_l = max(dot(s.normal, l), 1.0e-4);
    float no_h = max(dot(s.normal, h), 1.0e-4);
    float vo_h = max(dot(v, h), 1.0e-4);
    float alpha = max(s.roughness * s.roughness, 0.001);
    float d, vis;
    if (s.anisotropy > 1.0e-4) {
        float aspect = sqrt(max(1.0 - 0.9 * s.anisotropy, 0.1));
        float ax = max(alpha / aspect, 0.001);
        float ay = max(alpha * aspect, 0.001);
        d = D_GGX_Anisotropic(no_h, dot(s.tangent,h), dot(s.bitangent,h), ax, ay);
        vis = V_GGX_Anisotropic(
            no_v, no_l,
            dot(s.tangent,v), dot(s.bitangent,v),
            dot(s.tangent,l), dot(s.bitangent,l),
            ax, ay);
    } else {
        d = D_GGX(no_h, alpha);
        vis = V_GGX(no_v, no_l, alpha);
    }
    fresnel = FresnelSchlick(SurfaceF0(s), vo_h);
    fresnel = IridescentFresnel(fresnel, vo_h, s);
    return d * vis * fresnel;
}

float3 DirectBRDF(Surface s, float3 v, float3 l) {
    float no_l = saturate(dot(s.normal, l));
    float back_no_l = saturate(dot(-s.normal, l));
    if (no_l <= 0.0 && (s.diffuse_transmission <= 0.0 || back_no_l <= 0.0)) return 0.0.xxx;

    float3 result = 0.0.xxx;
    if (no_l > 0.0) {
        float3 h = normalize(v + l);
        float3 fresnel;
        float3 specular = SpecularBRDF(s, v, l, h, fresnel);
        float3 kd = (1.0.xxx - fresnel) * (1.0 - s.metallic);
        kd *= (1.0 - s.transmission) * (1.0 - s.diffuse_transmission);
        float3 diffuse = kd * s.albedo / PI;
        float3 base = (diffuse + specular) * no_l;

        if (s.clearcoat > 0.0) {
            float coat_no_v = max(dot(s.coat_normal, v), 1.0e-4);
            float coat_no_l = max(dot(s.coat_normal, l), 1.0e-4);
            float3 coat_h = normalize(v + l);
            float coat_no_h = max(dot(s.coat_normal, coat_h), 1.0e-4);
            float coat_vo_h = max(dot(v, coat_h), 1.0e-4);
            float coat_alpha = max(s.clearcoat_roughness * s.clearcoat_roughness, 0.001);
            float coat_f = FresnelSchlickScalar(0.04, coat_vo_h);
            float coat_spec = D_GGX(coat_no_h, coat_alpha) *
                V_GGX(coat_no_v, coat_no_l, coat_alpha) * coat_f;
            base *= 1.0 - s.clearcoat * coat_f;
            base += s.clearcoat * coat_spec * coat_no_l;
        }

        if (Max3(s.sheen_color) > 0.0) {
            float3 sheen_h = normalize(v + l);
            float sheen_d = D_Charlie(max(dot(s.normal, sheen_h), 1.0e-4), s.sheen_roughness);
            float sheen_v = V_Neubelt(max(dot(s.normal,v),1.0e-4), no_l);
            float3 sheen = s.sheen_color * sheen_d * sheen_v * no_l;
            float sheen_energy = saturate(Max3(s.sheen_color) * (1.0 - 0.5 * s.sheen_roughness));
            base *= 1.0 - sheen_energy;
            base += sheen;
        }
        result += base;
    }

    if (s.diffuse_transmission > 0.0 && back_no_l > 0.0) {
        result += s.albedo * s.diffuse_transmission_color *
            (1.0 - s.metallic) * s.diffuse_transmission * back_no_l / PI;
    }
    return result;
}

float3 TransmissionEnvironment(Surface s, float3 view) {
    if (s.transmission <= 0.0) return 0.0.xxx;
    float eta = 1.0 / s.ior;
    float dispersion = s.dispersion * 0.02;
    float3 direction_r = refract(-view, s.normal, 1.0 / max(s.ior + dispersion, 1.0001));
    float3 direction_g = refract(-view, s.normal, eta);
    float3 direction_b = refract(-view, s.normal, 1.0 / max(s.ior - dispersion, 1.0001));
    if (dot(direction_r,direction_r) < 1.0e-6) direction_r = reflect(-view,s.normal);
    if (dot(direction_g,direction_g) < 1.0e-6) direction_g = reflect(-view,s.normal);
    if (dot(direction_b,direction_b) < 1.0e-6) direction_b = reflect(-view,s.normal);
    float3 environment = float3(
        EnvironmentColor(direction_r).r,
        EnvironmentColor(direction_g).g,
        EnvironmentColor(direction_b).b);
    return environment * s.albedo * (1.0 - s.metallic) * s.transmission * VolumeAttenuation(s);
}

float3 EnvironmentSpecular(Surface s, float3 view) {
    float no_v = saturate(dot(s.normal, view));
    float3 f = FresnelSchlick(SurfaceF0(s), no_v);
    f = IridescentFresnel(f, no_v, s);
    float3 reflected = EnvironmentColor(reflect(-view, s.normal));
    float roughness_loss = 1.0 - 0.55 * s.roughness;
    return reflected * f * roughness_loss;
}

float ScreenAmbientOcclusion(float2 pixel_position) {
    if (PAmbientOcclusion.x == 0u) return 1.0;
    float2 size = max(
        float2((float)PAmbientOcclusion.y, (float)PAmbientOcclusion.z),
        1.0.xx
    );
    float2 uv = saturate(pixel_position / size);
    return saturate(AmbientOcclusion.SampleLevel(AmbientOcclusionSampler, uv, 0.0).r);
}

uint ShadowLightCount() { return (uint)max(PShadows[0].x, 0.0); }
uint ShadowViewBase() { return 1u + ShadowLightCount(); }
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
        reference = (depth - position_near.w) / max(forward_far.w - position_near.w, 1.0e-5);
    } else {
        ndc.x = dot(delta, right_scale.xyz) / max(up_scale.w * right_scale.w * depth, 1.0e-6);
        ndc.y = dot(delta, up_scale.xyz) / max(up_scale.w * depth, 1.0e-6);
        reference = (forward_far.w * depth - position_near.w * forward_far.w) /
            max((forward_far.w - position_near.w) * depth, 1.0e-5);
    }
    if (any(abs(ndc) > 1.0.xx)) return 1.0;
    float depth_bias = max(bias, 0.0) / max(forward_far.w - position_near.w, 1.0e-4);
    float3 sample_position = float3(ndc.x*0.5+0.5, 0.5-ndc.y*0.5, (float)((uint)meta.y));
    return ShadowMaps.SampleCmpLevelZero(ShadowSampler, sample_position, saturate(reference-depth_bias));
}
float ShadowVisibility(uint light_index, float3 position, float type, float bias) {
    uint light_count = ShadowLightCount();
    if (light_index >= light_count) return 1.0;
    float4 record = PShadows[1u + light_index];
    uint first = (uint)max(record.x, 0.0);
    uint count = (uint)max(record.y, 0.0);
    if (count == 0u || record.w < 0.5) return 1.0;
    uint selected = first;
    bool directional = type > 1.5 && type < 2.5;
    float camera_depth = 0.0;
    if (type < 1.5 && count > 1u) {
        float best = -2.0;
        for (uint index = 0u; index < count; ++index) {
            uint candidate = first + index;
            float3 origin = ShadowView(candidate, 0u).xyz;
            float3 direction = normalize(position - origin);
            float score = dot(direction, ShadowView(candidate, 1u).xyz);
            if (score > best) { best = score; selected = candidate; }
        }
    } else if (directional && count > 1u) {
        camera_depth = dot(position - PCameraPositionNear.xyz, PCameraForwardFar.xyz);
        selected = first + count - 1u;
        for (uint index = 0u; index < count; ++index) {
            uint candidate = first + index;
            if (camera_depth <= ShadowView(candidate, 4u).z) { selected = candidate; break; }
        }
    }

    float effective_bias = max(record.z, bias);
    float visibility = SampleShadow(selected, position, effective_bias);
    uint last = first + count - 1u;
    if (directional && selected < last) {
        float4 meta = ShadowView(selected, 4u);
        float blend_start = meta.w;
        float split = meta.z;
        if (camera_depth > blend_start && split > blend_start) {
            float next_visibility = SampleShadow(selected + 1u, position, effective_bias);
            float blend = saturate((camera_depth - blend_start) / (split - blend_start));
            visibility = lerp(visibility, next_visibility, blend);
        }
    }
    return visibility;
}

float3 ShadeLight(uint light_index, uint light_base, float3 position, Surface s, float3 view) {
    uint offset = light_base + light_index * 5u;
    float4 position_intensity = PGI[offset + 0u];
    float4 direction_type = PGI[offset + 1u];
    float4 color_range = PGI[offset + 2u];
    float4 cone_shadow = PGI[offset + 3u];
    if (position_intensity.w <= 0.0) return 0.0.xxx;

    float3 light_direction = normalize(direction_type.xyz);
    float3 l = -light_direction;
    float attenuation = 1.0;
    if (direction_type.w < 1.5 || direction_type.w > 2.5) {
        float3 to_light = position_intensity.xyz - position;
        float dist = length(to_light);
        if (dist <= 1.0e-5) return 0.0.xxx;
        l = to_light / dist;
        attenuation = 1.0 / max(dist * dist, 1.0);
        if (color_range.w > 0.0) attenuation *= saturate(1.0 - dist / color_range.w);
        if (direction_type.w > 2.5)
            attenuation *= smoothstep(cone_shadow.y, cone_shadow.x, dot(-l, light_direction));
    }
    if (attenuation <= 0.0) return 0.0.xxx;

    float visibility = 1.0;
    if (cone_shadow.z > 0.5) {
        float bias = max(cone_shadow.w, 1.0e-4);
        visibility = ShadowVisibility(light_index, position + s.normal * bias, direction_type.w, bias);
    }
    if (visibility <= 0.0) return 0.0.xxx;

    float3 brdf = DirectBRDF(s, view, l);
    return brdf * color_range.rgb * position_intensity.w * attenuation * visibility;
}

float3 DirectLighting(float3 position, Surface s, float3 view, float2 pixel_position) {
    float3 result = 0.0.xxx;
    uint light_count = (uint)max(PGI[11].z, 0.0);
    uint light_base = (uint)max(PGI[11].w, 12.0);

    if (PForward.x == 0u || PForward.y == 0u || PForward.z == 0u || PForward.w == 0u) {
        for (uint light_index = 0u; light_index < light_count; ++light_index)
            result += ShadeLight(light_index, light_base, position, s, view);
        return result;
    }

    uint tile_size = PForward.y;
    uint tiles_x = PForward.z;
    uint tiles_y = max(
        ((uint)max(PResolution.y, 1.0) + tile_size - 1u) / tile_size,
        1u
    );
    uint2 pixel = (uint2)max(pixel_position, 0.0.xx);
    uint tile_x = min(pixel.x / tile_size, tiles_x - 1u);
    uint tile_y = min(pixel.y / tile_size, tiles_y - 1u);
    uint tile = tile_y * tiles_x + tile_x;
    uint count = PForwardTileCounts[tile];

    if (count == 0xffffffffu) {
        for (uint light_index = 0u; light_index < light_count; ++light_index)
            result += ShadeLight(light_index, light_base, position, s, view);
        return result;
    }

    count = min(count, PForward.w);
    uint index_base = tile * PForward.w;
    for (uint index = 0u; index < count; ++index) {
        uint light_index = PForwardLightIndices[index_base + index];
        if (light_index < light_count)
            result += ShadeLight(light_index, light_base, position, s, view);
    }
    return result;
}

struct PSOut { float4 color : SV_Target0; float2 velocity : SV_Target1; };
PSOut PSMain(VSOut i, bool front_face : SV_IsFrontFace) {
    PSOut o;
    uint mi = min(i.material, (uint)max(PCounts.z - 1, 0));
    GpuBaseMaterial base = PBaseMaterials[mi];
    GpuMaterial material = PMaterials[mi];
    bool surface_front_face = i.mirrored != 0u ? !front_face : front_face;
    if (!surface_front_face && material.misc.z < 0.5) discard;
    Surface s = EvaluateSurface(i, base, material, surface_front_face);
    if (material.misc.w > 0.5 && material.misc.w < 1.5 && s.alpha < material.misc.x) discard;

    if (material.misc.y > 0.5) {
        o.color = float4(s.albedo + s.emission, s.alpha);
        o.velocity = 0.0.xx;
        return o;
    }

    float3 view = normalize(PCameraPositionNear.xyz - i.world);
    float3 direct = DirectLighting(i.world, s, view, i.position.xy);
    float screen_ao = ScreenAmbientOcclusion(i.position.xy);
    float3 diffuse_indirect = SampleGI(i.world, s.normal) * s.albedo *
        (1.0 - s.metallic) * (1.0 - s.transmission) * s.ao * screen_ao;
    float3 ambient = PGI[6].xyz * PGI[6].w * s.albedo *
        (1.0 - s.metallic) * (1.0 - s.transmission) * s.ao * screen_ao;
    float3 specular_environment = EnvironmentSpecular(s, view);
    float3 transmission = TransmissionEnvironment(s, view);
    float3 diffuse_transmission_environment = EnvironmentColor(-s.normal) * s.albedo *
        s.diffuse_transmission_color * s.diffuse_transmission * (1.0 - s.metallic);
    float3 color = direct + diffuse_indirect + ambient + specular_environment +
        transmission + diffuse_transmission_environment + s.emission;

    float distance_to_camera = length(i.world - PCameraPositionNear.xyz);
    float fog_factor = 0.0;
    if (PGI[5].w > 0.5 && PGI[5].w < 1.5)
        fog_factor = saturate((distance_to_camera - PGI[7].y) / max(PGI[7].z - PGI[7].y, 1.0e-5));
    else if (PGI[5].w > 1.5)
        fog_factor = 1.0 - exp(-max(PGI[7].x,0.0) * distance_to_camera);
    color = lerp(color, PGI[5].xyz, saturate(fog_factor));

    o.color = float4(max(color, 0.0.xxx), s.alpha);
    o.velocity = 0.0.xx;
    return o;
}
)HLSL";

} // namespace Renderer::SDLGPU::PBRShaders

#endif