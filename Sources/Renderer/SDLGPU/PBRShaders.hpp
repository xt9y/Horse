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
        o.position = float4(x, y, z, max(depth, 1.0e-5));
    }
    o.world = p;
    o.normal = n;
    o.uv = vertex.uv.xy;
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

float3 TangentNormal(float3 n, float3 tangent, float3 bitangent, int slot, float scale) {
    if (slot < 0) return normalize(n);
    float3 mapped = SampleSlot(slot, 0.0.xx).xyz;
    return normalize(n);
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
    s.roughness = max(saturate(material.pbr.x * mr.r), 0.04);
    s.metallic = saturate(material.pbr.y * mr.g);
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
            if (camera_depth <= ShadowView(candidate, 4u).z) { selected = candidate; break; }
        }
    }
    return SampleShadow(selected, position, max(record.z, bias));
}

float3 DirectLighting(float3 position, Surface s, float3 view) {
    float3 result = 0.0.xxx;
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
        float visibility = 1.0;
        if (cone_shadow.z > 0.5) {
            float bias = max(cone_shadow.w, 1.0e-4);
            visibility = ShadowVisibility(light_index, position + s.normal * bias, direction_type.w, bias);
        }
        if (visibility <= 0.0) continue;
        float3 brdf = DirectBRDF(s, view, l);
        result += brdf * color_range.rgb * position_intensity.w * attenuation * visibility;
    }
    return result;
}

struct PSOut { float4 color : SV_Target0; float2 velocity : SV_Target1; };
PSOut PSMain(VSOut i, bool front_face : SV_IsFrontFace) {
    PSOut o;
    uint mi = min(i.material, (uint)max(PCounts.z - 1, 0));
    GpuBaseMaterial base = PBaseMaterials[mi];
    GpuMaterial material = PMaterials[mi];
    if (!front_face && material.misc.z < 0.5) discard;
    Surface s = EvaluateSurface(i, base, material, front_face);
    if (material.misc.w > 0.5 && material.misc.w < 1.5 && s.alpha < material.misc.x) discard;

    if (material.misc.y > 0.5) {
        o.color = float4(s.albedo + s.emission, s.alpha);
        o.velocity = 0.0.xx;
        return o;
    }

    float3 view = normalize(PCameraPositionNear.xyz - i.world);
    float3 direct = DirectLighting(i.world, s, view);
    float3 diffuse_indirect = SampleGI(i.world, s.normal) * s.albedo *
        (1.0 - s.metallic) * (1.0 - s.transmission) * s.ao;
    float3 ambient = PGI[6].xyz * PGI[6].w * s.albedo *
        (1.0 - s.metallic) * (1.0 - s.transmission) * s.ao;
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
    float4 CameraPositionNear; float4 CameraForwardFar; float4 CameraRightAspect;
    float4 CameraUpTanHalfFov; float4 ProjectionAlpha; float4 Resolution;
    int4 Counts; uint4 Frame; uint4 PathPolicy;
};
static const float PI = 3.14159265359;

float4 SampleSlot(int slot,float2 uv){
    if(slot==0)return Tex0.SampleLevel(Samp0,uv,0); if(slot==1)return Tex1.SampleLevel(Samp1,uv,0);
    if(slot==2)return Tex2.SampleLevel(Samp2,uv,0); if(slot==3)return Tex3.SampleLevel(Samp3,uv,0);
    if(slot==4)return Tex4.SampleLevel(Samp4,uv,0); if(slot==5)return Tex5.SampleLevel(Samp5,uv,0);
    if(slot==6)return Tex6.SampleLevel(Samp6,uv,0); if(slot==7)return Tex7.SampleLevel(Samp7,uv,0);
    if(slot==8)return Tex8.SampleLevel(Samp8,uv,0); if(slot==9)return Tex9.SampleLevel(Samp9,uv,0);
    if(slot==10)return Tex10.SampleLevel(Samp10,uv,0); if(slot==11)return Tex11.SampleLevel(Samp11,uv,0);
    if(slot==12)return Tex12.SampleLevel(Samp12,uv,0); if(slot==13)return Tex13.SampleLevel(Samp13,uv,0);
    if(slot==14)return Tex14.SampleLevel(Samp14,uv,0); if(slot==15)return Tex15.SampleLevel(Samp15,uv,0);
    return 1.0.xxxx;
}
float3 SrgbToLinear(float3 c){c=saturate(c);float3 lo=c/12.92;float3 hi=pow((c+.055)/1.055,2.4);return lerp(lo,hi,step(.04045.xxx,c));}
float4 SampleColorSlot(int slot,float2 uv){float4 v=SampleSlot(slot,uv);v.rgb=SrgbToLinear(v.rgb);return v;}
float Max3(float3 v){return max(v.x,max(v.y,v.z));} float Sq(float x){return x*x;}
float3 Fresnel(float3 f0,float x){float f=pow(1-saturate(x),5);return f0+(1-f0)*f;}
float Fresnel1(float f0,float x){float f=pow(1-saturate(x),5);return f0+(1-f0)*f;}
float Dggx(float nh,float a){float a2=max(a*a,1e-6);float d=nh*nh*(a2-1)+1;return a2/max(PI*d*d,1e-6);}
float Vggx(float nv,float nl,float a){float a2=max(a*a,1e-6);float gv=nl*sqrt(max(nv*nv*(1-a2)+a2,0));float gl=nv*sqrt(max(nl*nl*(1-a2)+a2,0));return .5/max(gv+gl,1e-6);}
float Daniso(float nh,float th,float bh,float ax,float ay){float d=Sq(th/max(ax,1e-4))+Sq(bh/max(ay,1e-4))+nh*nh;return 1/max(PI*ax*ay*d*d,1e-6);}
float Vaniso(float nv,float nl,float tv,float bv,float tl,float bl,float ax,float ay){float lv=nl*length(float3(ax*tv,ay*bv,nv));float ll=nv*length(float3(ax*tl,ay*bl,nl));return .5/max(lv+ll,1e-6);}
float Dcharlie(float nh,float r){r=max(r,.02);float ir=1/r;float s=max(1-nh*nh,1e-6);return (2+ir)*pow(s,.5*ir)/(2*PI);}
float Vneubelt(float nv,float nl){return 1/max(4*(nl+nv-nl*nv),1e-5);}
uint Hash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float Random(inout uint s){s=Hash(s);return(s&0x00ffffffu)/16777216.0;}

bool IntersectAabb(float3 ro,float3 inv,float3 mn,float3 mx,float mt){float3 t0=(mn-ro)*inv,t1=(mx-ro)*inv;float3 a=min(t0,t1),b=max(t0,t1);float en=max(max(a.x,a.y),max(a.z,0.0));float ex=min(min(b.x,b.y),b.z);return ex>=en&&en<mt;}
bool IntersectTriangle(float3 ro,float3 rd,GpuTriangle t,out float d,out float u,out float v){d=0;u=0;v=0;float3 e1=t.p1.xyz-t.p0.xyz,e2=t.p2.xyz-t.p0.xyz,p=cross(rd,e2);float det=dot(e1,p);if(abs(det)<1e-7)return false;float id=1/det;float3 s=ro-t.p0.xyz;u=dot(s,p)*id;if(u<0||u>1)return false;float3 q=cross(s,e1);v=dot(rd,q)*id;if(v<0||u+v>1)return false;d=dot(e2,q)*id;return d>1e-4;}
struct Hit{bool hit;float t;uint triangle_index;float2 bary;};
Hit TraceClosest(float3 ro,float3 rd,float mt){Hit r;r.hit=false;r.t=mt;r.triangle_index=0;r.bary=0;if(Counts.x<=0||Counts.y<=0)return r;float3 safe=sign(rd+1e-20.xxx)*max(abs(rd),1e-8.xxx),inv=1/safe;uint st[64];uint top=0;st[top++]=0;while(top>0){uint ni=st[--top];if(ni>=(uint)Counts.x)continue;GpuNode n=Nodes[ni];if(!IntersectAabb(ro,inv,n.minimum,n.maximum,r.t))continue;if((n.meta&0x80000000u)!=0){uint c=n.meta&0x7fffffffu;for(uint i=0;i<c;i++){uint ti=n.first+i;if(ti>=(uint)Counts.y)continue;float t,u,v;if(IntersectTriangle(ro,rd,Triangles[ti],t,u,v)&&t<r.t){r.hit=true;r.t=t;r.triangle_index=ti;r.bary=float2(u,v);}}}else if(top+2<=64){st[top++]=n.first;st[top++]=n.meta;}}return r;}
bool Occluded(float3 ro,float3 rd,float mt){Hit h=TraceClosest(ro,rd,mt);return h.hit&&h.t<mt;}

float3 EnvironmentColor(float3 d){float3 sky=GI[4].xyz;float intensity=max(GI[3].w,0);if(GI[4].w>.5){float u=frac(atan2(d.z,d.x)/(2*PI)+.5+GI[7].w/(2*PI));float v=acos(clamp(d.y,-1,1))/PI;return SampleColorSlot(0,float2(u,v)).rgb*intensity;}return sky*max(intensity,1);}
float3 SampleGI(float3 p,float3 n){if(GI[0].w<.5||GI[2].w<1)return 0.0.xxx;float3 mn=GI[0].xyz,mx=GI[1].xyz;float intensity=max(GI[1].w,0);uint3 sz=(uint3)max(GI[2].xyz,1.0.xxx);float3 g=saturate((p-mn)/max(mx-mn,1e-6.xxx))*(float3(sz)-1);uint3 p0=(uint3)floor(g),p1=min(p0+1u,sz-1u);float3 f=g-float3(p0),c[4];[unroll]for(uint k=0;k<4;k++){uint i000=p0.x+sz.x*(p0.y+sz.y*p0.z),i100=p1.x+sz.x*(p0.y+sz.y*p0.z),i010=p0.x+sz.x*(p1.y+sz.y*p0.z),i110=p1.x+sz.x*(p1.y+sz.y*p0.z),i001=p0.x+sz.x*(p0.y+sz.y*p1.z),i101=p1.x+sz.x*(p0.y+sz.y*p1.z),i011=p0.x+sz.x*(p1.y+sz.y*p1.z),i111=p1.x+sz.x*(p1.y+sz.y*p1.z);float3 a=lerp(GI[12+i000*4+k].xyz,GI[12+i100*4+k].xyz,f.x),b=lerp(GI[12+i010*4+k].xyz,GI[12+i110*4+k].xyz,f.x),d=lerp(GI[12+i001*4+k].xyz,GI[12+i101*4+k].xyz,f.x),e=lerp(GI[12+i011*4+k].xyz,GI[12+i111*4+k].xyz,f.x);c[k]=lerp(lerp(a,b,f.y),lerp(d,e,f.y),f.z);}n=normalize(n);const float Y00=.28209479177,Y1=.48860251190;return max(c[0]*(PI*Y00)+c[1]*((2*PI/3)*Y1*n.x)+c[2]*((2*PI/3)*Y1*n.y)+c[3]*((2*PI/3)*Y1*n.z),0.0.xxx)*intensity;}
void CameraRay(float2 px,float2 jitter,out float3 ro,out float3 rd){float2 ndc=((px+jitter)/Resolution.xy)*2-1;ndc.y=-ndc.y;if(ProjectionAlpha.z>.5){ro=CameraPositionNear.xyz+CameraRightAspect.xyz*ndc.x*ProjectionAlpha.x+CameraUpTanHalfFov.xyz*ndc.y*ProjectionAlpha.y;rd=normalize(CameraForwardFar.xyz);}else{ro=CameraPositionNear.xyz;rd=normalize(CameraForwardFar.xyz+CameraRightAspect.xyz*ndc.x*CameraUpTanHalfFov.w*CameraRightAspect.w+CameraUpTanHalfFov.xyz*ndc.y*CameraUpTanHalfFov.w);}}

struct Surface{float3 position;float3 normal;float3 tangent;float3 bitangent;float2 uv;uint material;float3 albedo;float alpha;float roughness;float metallic;float ao;float3 emission;float specular_factor;float3 specular_color;float clearcoat;float clearcoat_roughness;float3 sheen_color;float sheen_roughness;float transmission;float thickness;float3 attenuation_color;float attenuation_distance;float anisotropy;float iridescence;float iridescence_ior;float iridescence_thickness;float dispersion;float ior;float diffuse_transmission;float3 diffuse_transmission_color;};

void TriangleBasis(GpuTriangle t,float3 n,out float3 tangent,out float3 bitangent){float3 e1=t.p1.xyz-t.p0.xyz,e2=t.p2.xyz-t.p0.xyz;float2 d1=t.uv01.zw-t.uv01.xy,d2=t.uv2.xy-t.uv01.xy;float det=d1.x*d2.y-d1.y*d2.x;if(abs(det)>1e-8){tangent=normalize((e1*d2.y-e2*d1.y)/det);tangent=normalize(tangent-n*dot(n,tangent));bitangent=normalize(cross(n,tangent))*(det<0?-1:1);if(all(isfinite(tangent))&&all(isfinite(bitangent)))return;}float3 up=abs(n.z)<.999?float3(0,0,1):float3(0,1,0);tangent=normalize(cross(up,n));bitangent=normalize(cross(n,tangent));}
float3 MapNormal(float3 n,float3 t,float3 b,int slot,float2 uv,float scale){if(slot<0)return n;float3 m=SampleSlot(slot,uv).xyz*2-1;m.xy*=scale;return normalize(t*m.x+b*m.y+n*m.z);}

Surface MakeSurface(Hit hit,float3 rd){
    GpuTriangle tri=Triangles[hit.triangle_index];float u=hit.bary.x,v=hit.bary.y,w=1-u-v;Surface s;s.position=tri.p0.xyz*w+tri.p1.xyz*u+tri.p2.xyz*v;s.normal=normalize(tri.n0.xyz*w+tri.n1.xyz*u+tri.n2.xyz*v);if(dot(s.normal,rd)>0)s.normal=-s.normal;s.uv=tri.uv01.xy*w+tri.uv01.zw*u+tri.uv2.xy*v;s.material=asuint(tri.p0.w);uint mi=min(s.material,(uint)max(Counts.z-1,0));GpuBaseMaterial base=BaseMaterials[mi];GpuMaterial m=Materials[mi];
    float4 bc=m.tex0.x>=0?SampleColorSlot(m.tex0.x,s.uv):1.0.xxxx;s.albedo=max(base.base_color.rgb*bc.rgb,0.0.xxx);s.alpha=base.base_color.a*bc.a;if(m.tex1.z>=0)s.alpha*=SampleSlot(m.tex1.z,s.uv).r;
    float4 mr=m.tex0.z>=0?SampleSlot(m.tex0.z,s.uv):1.0.xxxx;s.roughness=max(saturate(m.pbr.x*mr.r),.04);s.metallic=saturate(m.pbr.y*mr.g);s.ao=saturate(m.pbr.z*(m.tex1.x>=0?SampleSlot(m.tex1.x,s.uv).r:1));
    float3 bt,bb;TriangleBasis(tri,s.normal,bt,bb);s.normal=MapNormal(s.normal,bt,bb,m.tex0.y,s.uv,m.pbr.w);
    float4 an=m.tex4.z>=0?SampleSlot(m.tex4.z,s.uv):float4(.5,.5,1,1);s.anisotropy=saturate(m.anisotropy_iridescence.x*an.b);float2 dir=m.tex4.z>=0?an.rg*2-1:float2(1,0);if(dot(dir,dir)<1e-6)dir=float2(1,0);dir=normalize(dir);float sn=sin(m.anisotropy_iridescence.y),cs=cos(m.anisotropy_iridescence.y);dir=float2(dir.x*cs-dir.y*sn,dir.x*sn+dir.y*cs);s.tangent=normalize(bt*dir.x+bb*dir.y);s.tangent=normalize(s.tangent-s.normal*dot(s.normal,s.tangent));s.bitangent=normalize(cross(s.normal,s.tangent));
    s.emission=m.emissive_strength.rgb*m.emissive_strength.w;if(m.tex1.y>=0)s.emission*=SampleColorSlot(m.tex1.y,s.uv).rgb;
    float4 coat=m.tex1.w>=0?SampleSlot(m.tex1.w,s.uv):1.0.xxxx;s.clearcoat=saturate(m.clearcoat_sheen.x*coat.r);s.clearcoat_roughness=max(saturate(m.clearcoat_sheen.y*coat.g),.04);
    float4 sh=m.tex2.z>=0?SampleSlot(m.tex2.z,s.uv):1.0.xxxx;s.sheen_color=max(m.sheen_thickness.rgb*(m.tex2.z>=0?SrgbToLinear(sh.rgb):1.0.xxx),0.0.xxx);s.sheen_roughness=max(saturate(m.clearcoat_sheen.z*sh.a),.02);
    float4 tt=m.tex3.x>=0?SampleSlot(m.tex3.x,s.uv):1.0.xxxx;s.transmission=saturate(m.clearcoat_sheen.w*tt.r);s.thickness=max(m.sheen_thickness.w*tt.g,0);s.attenuation_color=max(m.attenuation.rgb,1e-4.xxx);s.attenuation_distance=max(m.attenuation.w,0);
    float4 sp=m.tex3.z>=0?SampleSlot(m.tex3.z,s.uv):1.0.xxxx;s.specular_factor=saturate(m.specular.x*sp.a);s.specular_color=max(m.specular.yzw*(m.tex3.z>=0?SrgbToLinear(sp.rgb):1.0.xxx),0.0.xxx);
    float4 iri=m.tex4.x>=0?SampleSlot(m.tex4.x,s.uv):1.0.xxxx;s.iridescence=saturate(m.anisotropy_iridescence.z*iri.r);s.iridescence_ior=max(m.anisotropy_iridescence.w,1);s.iridescence_thickness=m.tex4.x>=0?lerp(m.iridescence_dispersion_ior.x,m.iridescence_dispersion_ior.y,iri.g):m.iridescence_dispersion_ior.y;s.dispersion=max(m.iridescence_dispersion_ior.z,0);s.ior=max(m.iridescence_dispersion_ior.w,1.0001);
    float4 dt=m.tex4.w>=0?SampleSlot(m.tex4.w,s.uv):1.0.xxxx;s.diffuse_transmission=saturate(m.diffuse_transmission.w*dt.a);s.diffuse_transmission_color=max(m.diffuse_transmission.rgb*(m.tex4.w>=0?SrgbToLinear(dt.rgb):1.0.xxx),0.0.xxx);return s;
}

float3 F0(Surface s){float d=Sq((s.ior-1)/(s.ior+1));return lerp(saturate(d*s.specular_factor*s.specular_color),s.albedo,s.metallic);}
float3 IriF(float3 f,float vh,Surface s){if(s.iridescence<=0||s.iridescence_thickness<=0)return f;float eta=max(s.iridescence_ior,1);float cf=sqrt(max(1-max(1-vh*vh,0)/(eta*eta),0));float3 wl=float3(650,510,475),ph=4*PI*eta*s.iridescence_thickness*cf/wl,in=.5.xxx+.5*cos(ph);float fi=Sq((eta-1)/(eta+1));float3 film=saturate(fi.xxx+(1-fi.xxx)*in*.65);return lerp(f,saturate(f+(1-f)*film),s.iridescence);}
float3 Volume(Surface s){if(s.thickness<=0||s.attenuation_distance<=0)return 1.0.xxx;return pow(s.attenuation_color,s.thickness/max(s.attenuation_distance,1e-5));}
float3 Spec(Surface s,float3 v,float3 l,float3 h,out float3 fr){float nv=max(dot(s.normal,v),1e-4),nl=max(dot(s.normal,l),1e-4),nh=max(dot(s.normal,h),1e-4),vh=max(dot(v,h),1e-4),a=max(s.roughness*s.roughness,.001),d,vis;if(s.anisotropy>1e-4){float aspect=sqrt(max(1-.9*s.anisotropy,.1)),ax=max(a/aspect,.001),ay=max(a*aspect,.001);d=Daniso(nh,dot(s.tangent,h),dot(s.bitangent,h),ax,ay);vis=Vaniso(nv,nl,dot(s.tangent,v),dot(s.bitangent,v),dot(s.tangent,l),dot(s.bitangent,l),ax,ay);}else{d=Dggx(nh,a);vis=Vggx(nv,nl,a);}fr=IriF(Fresnel(F0(s),vh),vh,s);return d*vis*fr;}
float3 BRDF(Surface s,float3 v,float3 l){float nl=saturate(dot(s.normal,l)),bnl=saturate(dot(-s.normal,l));if(nl<=0&&(s.diffuse_transmission<=0||bnl<=0))return 0.0.xxx;float3 r=0;if(nl>0){float3 h=normalize(v+l),f;float3 spec=Spec(s,v,l,h,f),kd=(1-f)*(1-s.metallic)*(1-s.transmission)*(1-s.diffuse_transmission);float3 base=(kd*s.albedo/PI+spec)*nl;if(Max3(s.sheen_color)>0){float sd=Dcharlie(max(dot(s.normal,h),1e-4),s.sheen_roughness),sv=Vneubelt(max(dot(s.normal,v),1e-4),nl);float3 sheen=s.sheen_color*sd*sv*nl;base*=1-saturate(Max3(s.sheen_color)*(1-.5*s.sheen_roughness));base+=sheen;}if(s.clearcoat>0){float cf=Fresnel1(.04,max(dot(v,h),1e-4)),ca=max(s.clearcoat_roughness*s.clearcoat_roughness,.001),cs=Dggx(max(dot(s.normal,h),1e-4),ca)*Vggx(max(dot(s.normal,v),1e-4),nl,ca)*cf;base*=1-s.clearcoat*cf;base+=s.clearcoat*cs*nl;}r+=base;}if(s.diffuse_transmission>0&&bnl>0)r+=s.albedo*s.diffuse_transmission_color*(1-s.metallic)*s.diffuse_transmission*bnl/PI;return r;}
float3 DirectLight(Surface s,float3 view){float3 result=0;uint lc=(uint)max(GI[11].z,0),lb=(uint)max(GI[11].w,12);for(uint li=0;li<lc;li++){uint o=lb+li*5;float4 pi=GI[o],dt=GI[o+1],cr=GI[o+2],cs=GI[o+3];if(pi.w<=0)continue;float3 ld=normalize(dt.xyz),l=-ld;float att=1,mt=1e30;if(dt.w<1.5||dt.w>2.5){float3 delta=pi.xyz-s.position;float dist=length(delta);if(dist<=1e-5)continue;l=delta/dist;float bias=max(cs.w,1e-4);mt=max(dist-bias,0);att=1/max(dist*dist,1);if(cr.w>0)att*=saturate(1-dist/cr.w);if(dt.w>2.5)att*=smoothstep(cs.y,cs.x,dot(-l,ld));}if(att<=0)continue;if(cs.z>.5&&mt>0){float bias=max(cs.w,1e-4);if(Occluded(s.position+s.normal*bias,l,mt))continue;}result+=BRDF(s,view,l)*cr.rgb*pi.w*att;}return result;}
float3 EnvironmentSpec(Surface s,float3 v){float nv=saturate(dot(s.normal,v));float3 f=IriF(Fresnel(F0(s),nv),nv,s);return EnvironmentColor(reflect(-v,s.normal))*f*(1-.55*s.roughness);}
float3 TransmissionEnv(Surface s,float3 v){if(s.transmission<=0)return 0.0.xxx;float disp=s.dispersion*.02;float3 rr=refract(-v,s.normal,1/max(s.ior+disp,1.0001)),rg=refract(-v,s.normal,1/s.ior),rb=refract(-v,s.normal,1/max(s.ior-disp,1.0001));if(dot(rr,rr)<1e-6)rr=reflect(-v,s.normal);if(dot(rg,rg)<1e-6)rg=reflect(-v,s.normal);if(dot(rb,rb)<1e-6)rb=reflect(-v,s.normal);float3 e=float3(EnvironmentColor(rr).r,EnvironmentColor(rg).g,EnvironmentColor(rb).b);return e*s.albedo*(1-s.metallic)*s.transmission*Volume(s);}

float3 ShadeRay(float3 ro,float3 rd,out float depth){Hit h=TraceClosest(ro,rd,1e30);if(!h.hit){depth=0;return EnvironmentColor(rd);}depth=h.t;Surface s=MakeSurface(h,rd);GpuMaterial m=Materials[min(s.material,(uint)max(Counts.z-1,0))];if(m.misc.w>.5&&m.misc.w<1.5&&s.alpha<m.misc.x){Hit h2=TraceClosest(s.position+rd*.002,rd,1e30);if(!h2.hit)return EnvironmentColor(rd);s=MakeSurface(h2,rd);depth+=h2.t;}if(m.misc.y>.5)return s.albedo+s.emission;float3 v=normalize(ro-s.position);float3 direct=DirectLight(s,v),indirect=SampleGI(s.position,s.normal)*s.albedo*(1-s.metallic)*(1-s.transmission)*s.ao+GI[6].xyz*GI[6].w*s.albedo*(1-s.metallic)*(1-s.transmission)*s.ao;float3 dt=EnvironmentColor(-s.normal)*s.albedo*s.diffuse_transmission_color*s.diffuse_transmission*(1-s.metallic);return max(direct+indirect+EnvironmentSpec(s,v)+TransmissionEnv(s,v)+dt+s.emission,0.0.xxx);}
float3 CosineHemisphere(float3 n,inout uint rng){float r1=Random(rng),r2=Random(rng),phi=2*PI*r1,r=sqrt(r2);float3 t=normalize(abs(n.z)<.999?cross(float3(0,0,1),n):cross(float3(0,1,0),n)),b=cross(n,t);return normalize(t*(cos(phi)*r)+b*(sin(phi)*r)+n*sqrt(max(1-r2,0)));}

[numthreads(8,8,1)]void RayMain(uint3 tid:SV_DispatchThreadID){if(tid.x>=(uint)Resolution.x||tid.y>=(uint)Resolution.y)return;float3 ro,rd;CameraRay(float2(tid.xy),.5.xx,ro,rd);float depth;float3 color=ShadeRay(ro,rd,depth);Output[tid.xy]=float4(color,1);LinearDepth[tid.xy]=depth;Accumulation[tid.xy]=float4(color,1);}

[numthreads(8,8,1)]void PathMain(uint3 tid:SV_DispatchThreadID){
    uint width=(uint)Resolution.x,height=(uint)Resolution.y;if(tid.x>=width||tid.y>=height)return;bool reset=(Frame.w&1u)!=0,moving=(Frame.w&2u)!=0;if(reset)Accumulation[tid.xy]=0;
    if(moving){uint block=max(PathPolicy.w,1u);if((tid.x%block)==0&&(tid.y%block)==0){float2 px=min(float2(tid.xy)+float2(block*.5,block*.5),float2(width,height)-.5.xx);float3 dro,drd;CameraRay(px,0.0.xx,dro,drd);Hit dh=TraceClosest(dro,drd,1e30);float dv=dh.hit?dh.t:0;uint2 end=min(tid.xy+uint2(block,block),uint2(width,height));for(uint y=tid.y;y<end.y;y++)for(uint x=tid.x;x<end.x;x++)LinearDepth[uint2(x,y)]=dv;}}
    else if(reset){float3 dro,drd;CameraRay(float2(tid.xy),.5.xx,dro,drd);Hit dh=TraceClosest(dro,drd,1e30);LinearDepth[tid.xy]=dh.hit?dh.t:0;}
    uint grid=moving?max(PathPolicy.z,1u):(reset?max(PathPolicy.y,1u):max(PathPolicy.x,1u)),count=grid*grid,phase=Frame.x%max(count,1u),pixel=(tid.x%grid)+(tid.y%grid)*grid;if(pixel!=phase)return;
    uint rng=Hash(tid.x+tid.y*width+Frame.x*747796405u+1u);float3 sum=0;float first_depth=0;uint spp=max(Frame.z,1u);
    for(uint sample=0;sample<spp;sample++){float2 jitter=float2(Random(rng),Random(rng));float3 ro,rd;CameraRay(float2(tid.xy),jitter,ro,rd);float3 throughput=1.0.xxx,radiance=0;
        for(uint bounce=0;bounce<4;bounce++){Hit hit=TraceClosest(ro,rd,1e30);if(!hit.hit){radiance+=throughput*EnvironmentColor(rd);break;}if(bounce==0)first_depth=hit.t;Surface s=MakeSurface(hit,rd);GpuMaterial m=Materials[min(s.material,(uint)max(Counts.z-1,0))];if(m.misc.w>.5&&m.misc.w<1.5&&s.alpha<m.misc.x){ro=s.position+rd*.002;continue;}radiance+=throughput*s.emission;if(m.misc.y>.5){radiance+=throughput*s.albedo;break;}float3 v=-rd;radiance+=throughput*DirectLight(s,v);radiance+=throughput*SampleGI(s.position,s.normal)*s.albedo*.2*(1-s.metallic)*(1-s.transmission);
            float transmission_probability=s.transmission*(1-s.metallic);float diffuse_transmission_probability=s.diffuse_transmission*(1-s.metallic)*(1-transmission_probability);float3 f0=F0(s);float spec_probability=saturate(Max3(f0)+(1-s.roughness)*.2);float choice=Random(rng);
            if(choice<transmission_probability){float eta=dot(rd,s.normal)<0?1/s.ior:s.ior;float3 refracted=refract(rd,s.normal,eta);if(dot(refracted,refracted)<1e-6)refracted=reflect(rd,s.normal);rd=normalize(refracted);throughput*=s.albedo*Volume(s)/max(transmission_probability,.05);ro=s.position+rd*.002;}
            else if(choice<transmission_probability+diffuse_transmission_probability){rd=CosineHemisphere(-s.normal,rng);throughput*=s.albedo*s.diffuse_transmission_color/max(diffuse_transmission_probability,.05);ro=s.position-s.normal*.002;}
            else{float remaining=max(1-transmission_probability-diffuse_transmission_probability,.05);float local=Random(rng);if(local<spec_probability){float3 reflected=reflect(rd,s.normal);rd=normalize(lerp(reflected,CosineHemisphere(s.normal,rng),s.roughness*s.roughness));throughput*=Fresnel(f0,saturate(dot(-rd,s.normal)))/max(spec_probability*remaining,.05);}else{rd=CosineHemisphere(s.normal,rng);throughput*=s.albedo*(1-s.metallic)/max((1-spec_probability)*remaining,.05);}ro=s.position+s.normal*.002;}}
            if(bounce>=2){float survive=max(saturate(Max3(throughput)),.1);if(Random(rng)>survive)break;throughput/=survive;}
        }sum+=radiance;}
    float4 previous=reset?0.0.xxxx:Accumulation[tid.xy],acc=previous+float4(sum,spp);Accumulation[tid.xy]=acc;Output[tid.xy]=float4(acc.rgb/max(acc.a,1),1);if(!moving)LinearDepth[tid.xy]=first_depth;
}
)HLSL";

} // namespace Renderer::SDLGPU::PBRShaders

#endif
