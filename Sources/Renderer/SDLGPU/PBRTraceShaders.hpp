#ifndef HORSE_RENDERER_SDLGPU_PBR_TRACE_SHADERS_HPP
#define HORSE_RENDERER_SDLGPU_PBR_TRACE_SHADERS_HPP

namespace Renderer::SDLGPU::PBRTraceShaders {

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

static const float PI = 3.14159265359;

float4 SampleSlot(int slot, float2 uv) {
    if (slot == 0) return Tex0.SampleLevel(Samp0, uv, 0);
    if (slot == 1) return Tex1.SampleLevel(Samp1, uv, 0);
    if (slot == 2) return Tex2.SampleLevel(Samp2, uv, 0);
    if (slot == 3) return Tex3.SampleLevel(Samp3, uv, 0);
    if (slot == 4) return Tex4.SampleLevel(Samp4, uv, 0);
    if (slot == 5) return Tex5.SampleLevel(Samp5, uv, 0);
    if (slot == 6) return Tex6.SampleLevel(Samp6, uv, 0);
    if (slot == 7) return Tex7.SampleLevel(Samp7, uv, 0);
    if (slot == 8) return Tex8.SampleLevel(Samp8, uv, 0);
    if (slot == 9) return Tex9.SampleLevel(Samp9, uv, 0);
    if (slot == 10) return Tex10.SampleLevel(Samp10, uv, 0);
    if (slot == 11) return Tex11.SampleLevel(Samp11, uv, 0);
    if (slot == 12) return Tex12.SampleLevel(Samp12, uv, 0);
    if (slot == 13) return Tex13.SampleLevel(Samp13, uv, 0);
    if (slot == 14) return Tex14.SampleLevel(Samp14, uv, 0);
    if (slot == 15) return Tex15.SampleLevel(Samp15, uv, 0);
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
float3 Fresnel(float3 f0, float cos_theta) {
    float f = pow(1.0 - saturate(cos_theta), 5.0);
    return f0 + (1.0.xxx - f0) * f;
}
float FresnelScalar(float f0, float cos_theta) {
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
    float inverse = 1.0 / r;
    float sin2 = max(1.0 - no_h * no_h, 1.0e-6);
    return (2.0 + inverse) * pow(sin2, 0.5 * inverse) / (2.0 * PI);
}
float V_Neubelt(float no_v, float no_l) {
    return 1.0 / max(4.0 * (no_l + no_v - no_l * no_v), 1.0e-5);
}

uint Hash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float Random(inout uint state) {
    state = Hash(state);
    return (state & 0x00ffffffu) / 16777216.0;
}

bool IntersectAabb(float3 ro, float3 inv_rd, float3 minimum, float3 maximum, float max_t) {
    float3 t0 = (minimum - ro) * inv_rd;
    float3 t1 = (maximum - ro) * inv_rd;
    float3 mn = min(t0, t1), mx = max(t0, t1);
    float enter = max(max(mn.x, mn.y), max(mn.z, 0.0));
    float leave = min(min(mx.x, mx.y), mx.z);
    return leave >= enter && enter < max_t;
}
bool IntersectTriangle(
    float3 ro, float3 rd, GpuTriangle tri,
    out float t, out float u, out float v)
{
    t = 0.0; u = 0.0; v = 0.0;
    float3 e1 = tri.p1.xyz - tri.p0.xyz;
    float3 e2 = tri.p2.xyz - tri.p0.xyz;
    float3 p = cross(rd, e2);
    float determinant = dot(e1, p);
    if (abs(determinant) < 1.0e-7) return false;
    float inverse = 1.0 / determinant;
    float3 s = ro - tri.p0.xyz;
    u = dot(s, p) * inverse;
    if (u < 0.0 || u > 1.0) return false;
    float3 q = cross(s, e1);
    v = dot(rd, q) * inverse;
    if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, q) * inverse;
    return t > 1.0e-4;
}
struct Hit { bool hit; float t; uint triangle_index; float2 bary; };
Hit TraceClosest(float3 ro, float3 rd, float max_t) {
    Hit result;
    result.hit = false; result.t = max_t; result.triangle_index = 0; result.bary = 0.0.xx;
    if (Counts.x <= 0 || Counts.y <= 0) return result;
    float3 safe = rd;
    safe = sign(safe + 1.0e-20.xxx) * max(abs(safe), 1.0e-8.xxx);
    float3 inverse = 1.0 / safe;
    uint stack[64]; uint top = 0; stack[top++] = 0;
    while (top > 0) {
        uint node_index = stack[--top];
        if (node_index >= (uint)Counts.x) continue;
        GpuNode node = Nodes[node_index];
        if (!IntersectAabb(ro, inverse, node.minimum, node.maximum, result.t)) continue;
        if ((node.meta & 0x80000000u) != 0u) {
            uint count = node.meta & 0x7fffffffu;
            for (uint index = 0; index < count; ++index) {
                uint triangle_index = node.first + index;
                if (triangle_index >= (uint)Counts.y) continue;
                float t, u, v;
                if (IntersectTriangle(ro, rd, Triangles[triangle_index], t, u, v) && t < result.t) {
                    result.hit = true;
                    result.t = t;
                    result.triangle_index = triangle_index;
                    result.bary = float2(u, v);
                }
            }
        } else if (top + 2u <= 64u) {
            stack[top++] = node.first;
            stack[top++] = node.meta;
        }
    }
    return result;
}
bool Occluded(float3 ro, float3 rd, float max_t) {
    Hit hit = TraceClosest(ro, rd, max_t);
    return hit.hit && hit.t < max_t;
}

float3 EnvironmentColor(float3 direction) {
    float3 sky = GI[4].xyz;
    float intensity = max(GI[3].w, 0.0);
    if (GI[4].w > 0.5) {
        float u = frac(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5 + GI[7].w / (2.0 * PI));
        float v = acos(clamp(direction.y, -1.0, 1.0)) / PI;
        return SampleColorSlot(0, float2(u, v)).rgb * intensity;
    }
    return sky * max(intensity, 1.0);
}
float3 SampleGI(float3 position, float3 normal) {
    if (GI[0].w < 0.5 || GI[2].w < 1.0) return 0.0.xxx;
    float3 minimum = GI[0].xyz, maximum = GI[1].xyz;
    float intensity = max(GI[1].w, 0.0);
    uint3 size = (uint3)max(GI[2].xyz, 1.0.xxx);
    float3 g = saturate((position - minimum) / max(maximum - minimum, 1.0e-6.xxx)) *
        (float3(size) - 1.0);
    uint3 p0 = (uint3)floor(g), p1 = min(p0 + 1u, size - 1u);
    float3 f = g - float3(p0);
    float3 c[4];
    [unroll] for (uint k = 0; k < 4; ++k) {
        uint i000=p0.x+size.x*(p0.y+size.y*p0.z), i100=p1.x+size.x*(p0.y+size.y*p0.z);
        uint i010=p0.x+size.x*(p1.y+size.y*p0.z), i110=p1.x+size.x*(p1.y+size.y*p0.z);
        uint i001=p0.x+size.x*(p0.y+size.y*p1.z), i101=p1.x+size.x*(p0.y+size.y*p1.z);
        uint i011=p0.x+size.x*(p1.y+size.y*p1.z), i111=p1.x+size.x*(p1.y+size.y*p1.z);
        float3 a=lerp(GI[12+i000*4+k].xyz,GI[12+i100*4+k].xyz,f.x);
        float3 b=lerp(GI[12+i010*4+k].xyz,GI[12+i110*4+k].xyz,f.x);
        float3 d=lerp(GI[12+i001*4+k].xyz,GI[12+i101*4+k].xyz,f.x);
        float3 e=lerp(GI[12+i011*4+k].xyz,GI[12+i111*4+k].xyz,f.x);
        c[k]=lerp(lerp(a,b,f.y),lerp(d,e,f.y),f.z);
    }
    normal = normalize(normal);
    const float Y00=.28209479177, Y1=.48860251190;
    return max(
        c[0]*(PI*Y00)+c[1]*((2*PI/3)*Y1*normal.x)+
        c[2]*((2*PI/3)*Y1*normal.y)+c[3]*((2*PI/3)*Y1*normal.z),
        0.0.xxx) * intensity;
}
void CameraRay(float2 pixel, float2 jitter, out float3 origin, out float3 direction) {
    float2 ndc = ((pixel + jitter) / Resolution.xy) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    if (ProjectionAlpha.z > 0.5) {
        origin = CameraPositionNear.xyz + CameraRightAspect.xyz * ndc.x * ProjectionAlpha.x +
            CameraUpTanHalfFov.xyz * ndc.y * ProjectionAlpha.y;
        direction = normalize(CameraForwardFar.xyz);
    } else {
        origin = CameraPositionNear.xyz;
        direction = normalize(CameraForwardFar.xyz +
            CameraRightAspect.xyz * ndc.x * CameraUpTanHalfFov.w * CameraRightAspect.w +
            CameraUpTanHalfFov.xyz * ndc.y * CameraUpTanHalfFov.w);
    }
}

struct Surface {
    float3 position;
    float3 normal;
    float3 coat_normal;
    float3 tangent;
    float3 bitangent;
    float2 uv;
    uint material;
    float3 albedo;
    float alpha;
    float roughness;
    float metallic;
    float ao;
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

void TriangleBasis(GpuTriangle triangle, float3 normal, out float3 tangent, out float3 bitangent) {
    float3 e1 = triangle.p1.xyz - triangle.p0.xyz;
    float3 e2 = triangle.p2.xyz - triangle.p0.xyz;
    float2 d1 = triangle.uv01.zw - triangle.uv01.xy;
    float2 d2 = triangle.uv2.xy - triangle.uv01.xy;
    float determinant = d1.x * d2.y - d1.y * d2.x;
    if (abs(determinant) > 1.0e-8) {
        tangent = normalize((e1 * d2.y - e2 * d1.y) / determinant);
        tangent = normalize(tangent - normal * dot(normal, tangent));
        bitangent = normalize(cross(normal, tangent)) * (determinant < 0.0 ? -1.0 : 1.0);
        if (all(isfinite(tangent)) && all(isfinite(bitangent))) return;
    }
    float3 up = abs(normal.z) < 0.999 ? float3(0,0,1) : float3(0,1,0);
    tangent = normalize(cross(up, normal));
    bitangent = normalize(cross(normal, tangent));
}
float3 MapNormal(float3 normal, float3 tangent, float3 bitangent, int slot, float2 uv, float scale) {
    if (slot < 0) return normal;
    float3 mapped = SampleSlot(slot, uv).xyz * 2.0 - 1.0;
    mapped.xy *= scale;
    return normalize(tangent * mapped.x + bitangent * mapped.y + normal * mapped.z);
}

Surface MakeSurface(Hit hit, float3 ray_direction) {
    GpuTriangle triangle = Triangles[hit.triangle_index];
    float u = hit.bary.x, v = hit.bary.y, w = 1.0 - u - v;
    Surface surface;
    surface.position = triangle.p0.xyz*w + triangle.p1.xyz*u + triangle.p2.xyz*v;
    float3 geometric = normalize(triangle.n0.xyz*w + triangle.n1.xyz*u + triangle.n2.xyz*v);
    if (dot(geometric, ray_direction) > 0.0) geometric = -geometric;
    surface.uv = triangle.uv01.xy*w + triangle.uv01.zw*u + triangle.uv2.xy*v;
    surface.material = asuint(triangle.p0.w);
    uint material_index = min(surface.material, (uint)max(Counts.z - 1, 0));
    GpuBaseMaterial base = BaseMaterials[material_index];
    GpuMaterial material = Materials[material_index];

    float4 base_sample = material.tex0.x >= 0 ? SampleColorSlot(material.tex0.x, surface.uv) : 1.0.xxxx;
    surface.albedo = max(base.base_color.rgb * base_sample.rgb, 0.0.xxx);
    surface.alpha = base.base_color.a * base_sample.a;
    if (material.tex1.z >= 0) surface.alpha *= SampleSlot(material.tex1.z, surface.uv).r;
    float4 mr = material.tex0.z >= 0 ? SampleSlot(material.tex0.z, surface.uv) : 1.0.xxxx;
    surface.roughness = max(saturate(material.pbr.x * mr.r), 0.04);
    surface.metallic = saturate(material.pbr.y * mr.g);
    surface.ao = saturate(material.pbr.z *
        (material.tex1.x >= 0 ? SampleSlot(material.tex1.x, surface.uv).r : 1.0));

    float3 base_tangent, base_bitangent;
    TriangleBasis(triangle, geometric, base_tangent, base_bitangent);
    surface.normal = MapNormal(
        geometric, base_tangent, base_bitangent,
        material.tex0.y, surface.uv, material.pbr.w);
    surface.coat_normal = MapNormal(
        geometric, base_tangent, base_bitangent,
        material.tex2.y, surface.uv, max(asfloat(material.tex5.y), 0.0));

    float4 anisotropy_sample = material.tex4.z >= 0
        ? SampleSlot(material.tex4.z, surface.uv)
        : float4(0.5, 0.5, 1.0, 1.0);
    surface.anisotropy = saturate(material.anisotropy_iridescence.x * anisotropy_sample.b);
    float2 anisotropy_direction = material.tex4.z >= 0
        ? anisotropy_sample.rg * 2.0 - 1.0
        : float2(1.0, 0.0);
    if (dot(anisotropy_direction, anisotropy_direction) < 1.0e-6)
        anisotropy_direction = float2(1.0, 0.0);
    anisotropy_direction = normalize(anisotropy_direction);
    float sine = sin(material.anisotropy_iridescence.y);
    float cosine = cos(material.anisotropy_iridescence.y);
    anisotropy_direction = float2(
        anisotropy_direction.x*cosine-anisotropy_direction.y*sine,
        anisotropy_direction.x*sine+anisotropy_direction.y*cosine);
    surface.tangent = normalize(
        base_tangent * anisotropy_direction.x + base_bitangent * anisotropy_direction.y);
    surface.tangent = normalize(surface.tangent - surface.normal * dot(surface.normal, surface.tangent));
    surface.bitangent = normalize(cross(surface.normal, surface.tangent));

    surface.emission = material.emissive_strength.rgb * material.emissive_strength.w;
    if (material.tex1.y >= 0)
        surface.emission *= SampleColorSlot(material.tex1.y, surface.uv).rgb;

    float4 coat = material.tex1.w >= 0 ? SampleSlot(material.tex1.w, surface.uv) : 1.0.xxxx;
    surface.clearcoat = saturate(material.clearcoat_sheen.x * coat.r);
    surface.clearcoat_roughness = max(saturate(material.clearcoat_sheen.y * coat.g), 0.04);

    float4 sheen = material.tex2.z >= 0 ? SampleSlot(material.tex2.z, surface.uv) : 1.0.xxxx;
    surface.sheen_color = max(material.sheen_thickness.rgb *
        (material.tex2.z >= 0 ? SrgbToLinear(sheen.rgb) : 1.0.xxx), 0.0.xxx);
    surface.sheen_roughness = max(saturate(material.clearcoat_sheen.z * sheen.a), 0.02);

    float4 transmission_thickness = material.tex3.x >= 0
        ? SampleSlot(material.tex3.x, surface.uv)
        : 1.0.xxxx;
    surface.transmission = saturate(material.clearcoat_sheen.w * transmission_thickness.r);
    surface.thickness = max(material.sheen_thickness.w * transmission_thickness.g, 0.0);
    surface.attenuation_color = max(material.attenuation.rgb, 1.0e-4.xxx);
    surface.attenuation_distance = max(material.attenuation.w, 0.0);

    float4 specular = material.tex3.z >= 0 ? SampleSlot(material.tex3.z, surface.uv) : 1.0.xxxx;
    surface.specular_factor = saturate(material.specular.x * specular.a);
    surface.specular_color = max(material.specular.yzw *
        (material.tex3.z >= 0 ? SrgbToLinear(specular.rgb) : 1.0.xxx), 0.0.xxx);

    float4 iridescence = material.tex4.x >= 0 ? SampleSlot(material.tex4.x, surface.uv) : 1.0.xxxx;
    surface.iridescence = saturate(material.anisotropy_iridescence.z * iridescence.r);
    surface.iridescence_ior = max(material.anisotropy_iridescence.w, 1.0);
    surface.iridescence_thickness = material.tex4.x >= 0
        ? lerp(material.iridescence_dispersion_ior.x, material.iridescence_dispersion_ior.y, iridescence.g)
        : material.iridescence_dispersion_ior.y;
    surface.dispersion = max(material.iridescence_dispersion_ior.z, 0.0);
    surface.ior = max(material.iridescence_dispersion_ior.w, 1.0001);

    float4 diffuse_transmission = material.tex4.w >= 0
        ? SampleSlot(material.tex4.w, surface.uv)
        : 1.0.xxxx;
    surface.diffuse_transmission = saturate(material.diffuse_transmission.w * diffuse_transmission.a);
    surface.diffuse_transmission_color = max(material.diffuse_transmission.rgb *
        (material.tex4.w >= 0 ? SrgbToLinear(diffuse_transmission.rgb) : 1.0.xxx), 0.0.xxx);
    return surface;
}

float3 SurfaceF0(Surface surface) {
    float dielectric = Square((surface.ior - 1.0) / (surface.ior + 1.0));
    return lerp(
        saturate(dielectric * surface.specular_factor * surface.specular_color),
        surface.albedo,
        surface.metallic);
}
float3 IridescentFresnel(float3 fresnel, float vo_h, Surface surface) {
    if (surface.iridescence <= 0.0 || surface.iridescence_thickness <= 0.0) return fresnel;
    float eta = max(surface.iridescence_ior, 1.0);
    float cos_film = sqrt(max(1.0 - max(1.0 - vo_h*vo_h, 0.0)/(eta*eta), 0.0));
    float3 wavelength = float3(650.0, 510.0, 475.0);
    float3 phase = 4.0 * PI * eta * surface.iridescence_thickness * cos_film / wavelength;
    float3 interference = 0.5.xxx + 0.5 * cos(phase);
    float interface_f0 = Square((eta - 1.0) / (eta + 1.0));
    float3 film = saturate(interface_f0.xxx + (1.0.xxx-interface_f0.xxx)*interference*0.65);
    return lerp(fresnel, saturate(fresnel + (1.0.xxx-fresnel)*film), surface.iridescence);
}
float3 VolumeAttenuation(Surface surface) {
    if (surface.thickness <= 0.0 || surface.attenuation_distance <= 0.0) return 1.0.xxx;
    return pow(
        surface.attenuation_color,
        surface.thickness / max(surface.attenuation_distance, 1.0e-5));
}
float3 SpecularBRDF(Surface surface, float3 view, float3 light, float3 half_vector, out float3 fresnel) {
    float no_v=max(dot(surface.normal,view),1.0e-4), no_l=max(dot(surface.normal,light),1.0e-4);
    float no_h=max(dot(surface.normal,half_vector),1.0e-4), vo_h=max(dot(view,half_vector),1.0e-4);
    float alpha=max(surface.roughness*surface.roughness,0.001), d, visibility;
    if (surface.anisotropy > 1.0e-4) {
        float aspect=sqrt(max(1.0-0.9*surface.anisotropy,0.1));
        float ax=max(alpha/aspect,0.001), ay=max(alpha*aspect,0.001);
        d=D_GGX_Anisotropic(no_h,dot(surface.tangent,half_vector),dot(surface.bitangent,half_vector),ax,ay);
        visibility=V_GGX_Anisotropic(
            no_v,no_l,
            dot(surface.tangent,view),dot(surface.bitangent,view),
            dot(surface.tangent,light),dot(surface.bitangent,light),ax,ay);
    } else {
        d=D_GGX(no_h,alpha);
        visibility=V_GGX(no_v,no_l,alpha);
    }
    fresnel=IridescentFresnel(Fresnel(SurfaceF0(surface),vo_h),vo_h,surface);
    return d*visibility*fresnel;
}
float3 DirectBRDF(Surface surface, float3 view, float3 light) {
    float no_l=saturate(dot(surface.normal,light));
    float back_no_l=saturate(dot(-surface.normal,light));
    if(no_l<=0.0&&(surface.diffuse_transmission<=0.0||back_no_l<=0.0)) return 0.0.xxx;
    float3 result=0.0.xxx;
    if(no_l>0.0) {
        float3 half_vector=normalize(view+light), fresnel;
        float3 specular=SpecularBRDF(surface,view,light,half_vector,fresnel);
        float3 kd=(1.0.xxx-fresnel)*(1.0-surface.metallic)*
            (1.0-surface.transmission)*(1.0-surface.diffuse_transmission);
        float3 base=(kd*surface.albedo/PI+specular)*no_l;

        if(Max3(surface.sheen_color)>0.0) {
            float sheen_d=D_Charlie(max(dot(surface.normal,half_vector),1.0e-4),surface.sheen_roughness);
            float sheen_v=V_Neubelt(max(dot(surface.normal,view),1.0e-4),no_l);
            float3 sheen=surface.sheen_color*sheen_d*sheen_v*no_l;
            base*=1.0-saturate(Max3(surface.sheen_color)*(1.0-0.5*surface.sheen_roughness));
            base+=sheen;
        }

        if(surface.clearcoat>0.0) {
            float coat_no_v=max(dot(surface.coat_normal,view),1.0e-4);
            float coat_no_l=max(dot(surface.coat_normal,light),1.0e-4);
            float coat_no_h=max(dot(surface.coat_normal,half_vector),1.0e-4);
            float coat_f=FresnelScalar(0.04,max(dot(view,half_vector),1.0e-4));
            float coat_alpha=max(surface.clearcoat_roughness*surface.clearcoat_roughness,0.001);
            float coat_spec=D_GGX(coat_no_h,coat_alpha)*V_GGX(coat_no_v,coat_no_l,coat_alpha)*coat_f;
            base*=1.0-surface.clearcoat*coat_f;
            base+=surface.clearcoat*coat_spec*coat_no_l;
        }
        result+=base;
    }
    if(surface.diffuse_transmission>0.0&&back_no_l>0.0) {
        result+=surface.albedo*surface.diffuse_transmission_color*(1.0-surface.metallic)*
            surface.diffuse_transmission*back_no_l/PI;
    }
    return result;
}

float3 DirectLight(Surface surface, float3 view) {
    float3 result=0.0.xxx;
    uint light_count=(uint)max(GI[11].z,0.0), light_base=(uint)max(GI[11].w,12.0);
    for(uint light_index=0; light_index<light_count; ++light_index) {
        uint offset=light_base+light_index*5u;
        float4 position_intensity=GI[offset], direction_type=GI[offset+1u];
        float4 color_range=GI[offset+2u], cone_shadow=GI[offset+3u];
        if(position_intensity.w<=0.0) continue;
        float3 light_direction=normalize(direction_type.xyz), light=-light_direction;
        float attenuation=1.0, max_t=1.0e30;
        if(direction_type.w<1.5||direction_type.w>2.5) {
            float3 delta=position_intensity.xyz-surface.position;
            float distance=length(delta);
            if(distance<=1.0e-5) continue;
            light=delta/distance;
            float bias=max(cone_shadow.w,1.0e-4);
            max_t=max(distance-bias,0.0);
            attenuation=1.0/max(distance*distance,1.0);
            if(color_range.w>0.0) attenuation*=saturate(1.0-distance/color_range.w);
            if(direction_type.w>2.5)
                attenuation*=smoothstep(cone_shadow.y,cone_shadow.x,dot(-light,light_direction));
        }
        if(attenuation<=0.0) continue;
        if(cone_shadow.z>0.5&&max_t>0.0) {
            float bias=max(cone_shadow.w,1.0e-4);
            if(Occluded(surface.position+surface.normal*bias,light,max_t)) continue;
        }
        result+=DirectBRDF(surface,view,light)*color_range.rgb*position_intensity.w*attenuation;
    }
    return result;
}
float3 EnvironmentSpecular(Surface surface,float3 view) {
    float no_v=saturate(dot(surface.normal,view));
    float3 f=IridescentFresnel(Fresnel(SurfaceF0(surface),no_v),no_v,surface);
    return EnvironmentColor(reflect(-view,surface.normal))*f*(1.0-0.55*surface.roughness);
}
float3 TransmissionEnvironment(Surface surface,float3 view) {
    if(surface.transmission<=0.0) return 0.0.xxx;
    float dispersion=surface.dispersion*0.02;
    float3 red=refract(-view,surface.normal,1.0/max(surface.ior+dispersion,1.0001));
    float3 green=refract(-view,surface.normal,1.0/surface.ior);
    float3 blue=refract(-view,surface.normal,1.0/max(surface.ior-dispersion,1.0001));
    if(dot(red,red)<1.0e-6) red=reflect(-view,surface.normal);
    if(dot(green,green)<1.0e-6) green=reflect(-view,surface.normal);
    if(dot(blue,blue)<1.0e-6) blue=reflect(-view,surface.normal);
    float3 environment=float3(EnvironmentColor(red).r,EnvironmentColor(green).g,EnvironmentColor(blue).b);
    return environment*surface.albedo*(1.0-surface.metallic)*surface.transmission*VolumeAttenuation(surface);
}

float3 ShadeRay(float3 origin,float3 direction,out float depth) {
    Hit hit=TraceClosest(origin,direction,1.0e30);
    if(!hit.hit){depth=0.0;return EnvironmentColor(direction);}
    depth=hit.t;
    Surface surface=MakeSurface(hit,direction);
    GpuMaterial material=Materials[min(surface.material,(uint)max(Counts.z-1,0))];
    if(material.misc.w>0.5&&material.misc.w<1.5&&surface.alpha<material.misc.x) {
        Hit second=TraceClosest(surface.position+direction*0.002,direction,1.0e30);
        if(!second.hit) return EnvironmentColor(direction);
        surface=MakeSurface(second,direction);
        depth+=second.t;
        material=Materials[min(surface.material,(uint)max(Counts.z-1,0))];
    }
    if(material.misc.y>0.5) return surface.albedo+surface.emission;
    float3 view=normalize(origin-surface.position);
    float3 indirect=SampleGI(surface.position,surface.normal)*surface.albedo*
        (1.0-surface.metallic)*(1.0-surface.transmission)*surface.ao;
    indirect+=GI[6].xyz*GI[6].w*surface.albedo*
        (1.0-surface.metallic)*(1.0-surface.transmission)*surface.ao;
    float3 diffuse_transmission=EnvironmentColor(-surface.normal)*surface.albedo*
        surface.diffuse_transmission_color*surface.diffuse_transmission*(1.0-surface.metallic);
    return max(
        DirectLight(surface,view)+indirect+EnvironmentSpecular(surface,view)+
        TransmissionEnvironment(surface,view)+diffuse_transmission+surface.emission,
        0.0.xxx);
}

float3 CosineHemisphere(float3 normal,inout uint rng) {
    float r1=Random(rng),r2=Random(rng),phi=2.0*PI*r1,r=sqrt(r2);
    float3 tangent=normalize(abs(normal.z)<0.999
        ?cross(float3(0,0,1),normal)
        :cross(float3(0,1,0),normal));
    float3 bitangent=cross(normal,tangent);
    return normalize(
        tangent*(cos(phi)*r)+bitangent*(sin(phi)*r)+normal*sqrt(max(1.0-r2,0.0)));
}

[numthreads(8,8,1)]
void RayMain(uint3 tid:SV_DispatchThreadID) {
    if(tid.x>=(uint)Resolution.x||tid.y>=(uint)Resolution.y) return;
    float3 origin,direction;
    CameraRay(float2(tid.xy),0.5.xx,origin,direction);
    float depth;
    float3 color=ShadeRay(origin,direction,depth);
    Output[tid.xy]=float4(color,1.0);
    LinearDepth[tid.xy]=depth;
    Accumulation[tid.xy]=float4(color,1.0);
}

[numthreads(8,8,1)]
void PathMain(uint3 tid:SV_DispatchThreadID) {
    uint width=(uint)Resolution.x,height=(uint)Resolution.y;
    if(tid.x>=width||tid.y>=height) return;
    bool reset=(Frame.w&1u)!=0u;
    bool moving=(Frame.w&2u)!=0u;
    if(reset) Accumulation[tid.xy]=0.0.xxxx;

    if(moving) {
        uint block=max(PathPolicy.w,1u);
        if((tid.x%block)==0u&&(tid.y%block)==0u) {
            float2 sample_pixel=min(
                float2(tid.xy)+float2(block*0.5,block*0.5),
                float2(width,height)-0.5.xx);
            float3 depth_origin,depth_direction;
            CameraRay(sample_pixel,0.0.xx,depth_origin,depth_direction);
            Hit depth_hit=TraceClosest(depth_origin,depth_direction,1.0e30);
            float depth_value=depth_hit.hit?depth_hit.t:0.0;
            uint2 end=min(tid.xy+uint2(block,block),uint2(width,height));
            for(uint y=tid.y;y<end.y;++y)
                for(uint x=tid.x;x<end.x;++x)
                    LinearDepth[uint2(x,y)]=depth_value;
        }
    } else if(reset) {
        float3 depth_origin,depth_direction;
        CameraRay(float2(tid.xy),0.5.xx,depth_origin,depth_direction);
        Hit depth_hit=TraceClosest(depth_origin,depth_direction,1.0e30);
        LinearDepth[tid.xy]=depth_hit.hit?depth_hit.t:0.0;
    }

    uint phase_grid=moving?max(PathPolicy.z,1u):(reset?max(PathPolicy.y,1u):max(PathPolicy.x,1u));
    uint phase_count=phase_grid*phase_grid;
    uint phase=Frame.x%max(phase_count,1u);
    uint pixel_phase=(tid.x%phase_grid)+(tid.y%phase_grid)*phase_grid;
    if(pixel_phase!=phase) return;

    uint rng=Hash(tid.x+tid.y*width+Frame.x*747796405u+1u);
    float3 sum=0.0.xxx;
    float first_depth=0.0;
    uint spp=max(Frame.z,1u);

    for(uint sample=0;sample<spp;++sample) {
        float2 jitter=float2(Random(rng),Random(rng));
        float3 origin,direction;
        CameraRay(float2(tid.xy),jitter,origin,direction);
        float3 throughput=1.0.xxx;
        float3 radiance=0.0.xxx;

        for(uint bounce=0;bounce<4u;++bounce) {
            Hit hit=TraceClosest(origin,direction,1.0e30);
            if(!hit.hit) {
                radiance+=throughput*EnvironmentColor(direction);
                break;
            }
            if(bounce==0u) first_depth=hit.t;
            Surface surface=MakeSurface(hit,direction);
            GpuMaterial material=Materials[min(surface.material,(uint)max(Counts.z-1,0))];
            if(material.misc.w>0.5&&material.misc.w<1.5&&surface.alpha<material.misc.x) {
                origin=surface.position+direction*0.002;
                continue;
            }
            radiance+=throughput*surface.emission;
            if(material.misc.y>0.5) {
                radiance+=throughput*surface.albedo;
                break;
            }

            float3 view=-direction;
            radiance+=throughput*DirectLight(surface,view);
            radiance+=throughput*SampleGI(surface.position,surface.normal)*surface.albedo*
                0.2*(1.0-surface.metallic)*(1.0-surface.transmission);

            float transmission_probability=surface.transmission*(1.0-surface.metallic);
            float diffuse_transmission_probability=surface.diffuse_transmission*(1.0-surface.metallic)*
                (1.0-transmission_probability);
            float3 f0=SurfaceF0(surface);
            float specular_probability=saturate(Max3(f0)+(1.0-surface.roughness)*0.2);
            float choice=Random(rng);

            if(choice<transmission_probability) {
                float eta=dot(direction,surface.normal)<0.0?1.0/surface.ior:surface.ior;
                float3 refracted=refract(direction,surface.normal,eta);
                if(dot(refracted,refracted)<1.0e-6) refracted=reflect(direction,surface.normal);
                direction=normalize(refracted);
                throughput*=surface.albedo*VolumeAttenuation(surface)/
                    max(transmission_probability,0.05);
                origin=surface.position+direction*0.002;
            } else if(choice<transmission_probability+diffuse_transmission_probability) {
                direction=CosineHemisphere(-surface.normal,rng);
                throughput*=surface.albedo*surface.diffuse_transmission_color/
                    max(diffuse_transmission_probability,0.05);
                origin=surface.position-surface.normal*0.002;
            } else {
                float remaining=max(
                    1.0-transmission_probability-diffuse_transmission_probability,
                    0.05);
                if(Random(rng)<specular_probability) {
                    float3 incident=direction;
                    float3 reflected=reflect(incident,surface.normal);
                    direction=normalize(lerp(
                        reflected,
                        CosineHemisphere(surface.normal,rng),
                        surface.roughness*surface.roughness));
                    float3 fresnel=Fresnel(f0,saturate(dot(-incident,surface.normal)));
                    throughput*=fresnel/max(specular_probability*remaining,0.05);
                } else {
                    direction=CosineHemisphere(surface.normal,rng);
                    throughput*=surface.albedo*(1.0-surface.metallic)/
                        max((1.0-specular_probability)*remaining,0.05);
                }
                origin=surface.position+surface.normal*0.002;
            }

            if(bounce>=2u) {
                float survive=max(saturate(Max3(throughput)),0.1);
                if(Random(rng)>survive) break;
                throughput/=survive;
            }
        }
        sum+=radiance;
    }

    float4 previous=reset?0.0.xxxx:Accumulation[tid.xy];
    float4 accumulated=previous+float4(sum,spp);
    Accumulation[tid.xy]=accumulated;
    Output[tid.xy]=float4(accumulated.rgb/max(accumulated.a,1.0),1.0);
    if(!moving) LinearDepth[tid.xy]=first_depth;
}
)HLSL";

} // namespace Renderer::SDLGPU::PBRTraceShaders

#endif
