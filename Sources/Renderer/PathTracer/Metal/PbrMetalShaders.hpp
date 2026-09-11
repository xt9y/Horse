#ifndef HORSE_RENDERER_PATHTRACER_METAL_PBR_METAL_SHADERS_HPP
#define HORSE_RENDERER_PATHTRACER_METAL_PBR_METAL_SHADERS_HPP

namespace Renderer::PbrMetalShaders {

inline constexpr const char *source = R"MSL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace raytracing;

struct Triangle {
    float4 p0; float4 p1; float4 p2;
    float4 n0; float4 n1; float4 n2;
    float4 uv01; float4 uv2;
};
struct Material { float4 base_color; int4 data; };
struct TraceUniforms {
    float4 camera_position;
    float4 camera_forward;
    float4 camera_right;
    float4 camera_up;
    float4 light_position_intensity;
    float4 light_color_tan_half_fov;
    float4 resolution_aspect;
    int4 counts;
    uint4 frame;
    uint4 path_policy;
};
struct ResolveUniforms { uint4 params; };
struct Hit {
    bool found;
    float distance;
    float3 position;
    float3 normal;
    float3 geometric_normal;
    float2 uv;
    uint material;
    uint triangle;
};
struct Surface {
    float3 albedo;
    float3 normal;
    float3 emissive;
    float roughness;
    float metallic;
    float ao;
    float clearcoat;
};

constant uint NO_TEXTURE = 31u;
constant float PI = 3.14159265358979323846f;
constant float RAY_EPSILON = 0.0025f;
constant float INF = 1.0e30f;
constant float SH_Y00 = 0.2820947918f;
constant float SH_Y1 = 0.4886025119f;
constant int GI_HEADER_VEC4S = 12;
constant int MAX_ALPHA_LAYERS = 16;

uint hashUint(uint value)
{
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    value ^= value >> 16; return value;
}

float randomFloat(thread uint& state)
{
    state = hashUint(state);
    return float(state) * (1.0f / 4294967296.0f);
}

float4 sampleTextureSlot(
    int slot,
    float2 uv,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    float2 p = float2(uv.x, 1.0f - uv.y);
    switch (slot) {
        case 0: return textures[0].sample(material_sampler, p);
        case 1: return textures[1].sample(material_sampler, p);
        case 2: return textures[2].sample(material_sampler, p);
        case 3: return textures[3].sample(material_sampler, p);
        case 4: return textures[4].sample(material_sampler, p);
        case 5: return textures[5].sample(material_sampler, p);
        case 6: return textures[6].sample(material_sampler, p);
        case 7: return textures[7].sample(material_sampler, p);
        case 8: return textures[8].sample(material_sampler, p);
        case 9: return textures[9].sample(material_sampler, p);
        case 10: return textures[10].sample(material_sampler, p);
        case 11: return textures[11].sample(material_sampler, p);
        case 12: return textures[12].sample(material_sampler, p);
        case 13: return textures[13].sample(material_sampler, p);
        case 14: return textures[14].sample(material_sampler, p);
        case 15: return textures[15].sample(material_sampler, p);
        case 16: return textures[16].sample(material_sampler, p);
        case 17: return textures[17].sample(material_sampler, p);
        case 18: return textures[18].sample(material_sampler, p);
        case 19: return textures[19].sample(material_sampler, p);
        case 20: return textures[20].sample(material_sampler, p);
        case 21: return textures[21].sample(material_sampler, p);
        case 22: return textures[22].sample(material_sampler, p);
        case 23: return textures[23].sample(material_sampler, p);
        case 24: return textures[24].sample(material_sampler, p);
        case 25: return textures[25].sample(material_sampler, p);
        case 26: return textures[26].sample(material_sampler, p);
        case 27: return textures[27].sample(material_sampler, p);
        case 28: return textures[28].sample(material_sampler, p);
        case 29: return textures[29].sample(material_sampler, p);
        case 30: return textures[30].sample(material_sampler, p);
        case 31: return textures[31].sample(material_sampler, p);
        default: return float4(1.0f);
    }
}

int packedTextureSlot(Material material, int index)
{
    uint slot = (uint(material.data.y) >> uint(index * 5)) & 31u;
    return slot == NO_TEXTURE ? -1 : int(slot);
}

float packedParameter(Material material, int index)
{
    return float((uint(material.data.z) >> uint(index * 8)) & 255u) * (1.0f / 255.0f);
}

float3 packedEmissiveColor(Material material)
{
    uint packed = uint(material.data.w);
    return float3(
        float((packed >> 0u) & 255u),
        float((packed >> 8u) & 255u),
        float((packed >> 16u) & 255u)
    ) * (1.0f / 255.0f);
}

float packedEmissiveStrength(Material material)
{
    return float((uint(material.data.w) >> 24u) & 255u) * (16.0f / 255.0f);
}

float2 triangleUv(Triangle triangle, float2 barycentric)
{
    float w0 = 1.0f - barycentric.x - barycentric.y;
    return triangle.uv01.xy * w0 + triangle.uv01.zw * barycentric.x + triangle.uv2.xy * barycentric.y;
}

bool alphaCutoutPass(
    uint material_index,
    float2 uv,
    device const Material *materials,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    int count = max(uniforms.counts.z, 0);
    if (material_index >= uint(count)) return true;
    Material material = materials[material_index];
    float alpha = clamp(material.base_color.a, 0.0f, 1.0f);
    if (material.data.x >= 0 && material.data.x < 32)
        alpha *= sampleTextureSlot(material.data.x, uv, textures, material_sampler).a;
    int opacity_slot = packedTextureSlot(material, 5);
    if (opacity_slot >= 0 && opacity_slot < 32)
        alpha *= sampleTextureSlot(opacity_slot, uv, textures, material_sampler).r;
    return alpha >= uniforms.resolution_aspect.w;
}

Hit traceClosestAlpha(
    float3 origin,
    float3 direction,
    float maximum_distance,
    bool apply_visibility,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    Hit hit;
    hit.found = false; hit.distance = maximum_distance; hit.position = float3(0.0f);
    hit.normal = float3(0.0f, 1.0f, 0.0f); hit.geometric_normal = hit.normal;
    hit.uv = float2(0.0f); hit.material = 0u; hit.triangle = 0u;
    int triangle_count = max(uniforms.counts.y, 0);
    if (triangle_count <= 0) return hit;

    bool alpha_cutouts = (uniforms.counts.w & 1) != 0;
    bool visibility_all = (uniforms.counts.w & 2) != 0;
    intersector<triangle_data> query;
    query.assume_geometry_type(geometry_type::triangle);
    query.assume_identity_transforms(true);
    float3 current_origin = origin;
    float travelled = 0.0f;

    for (int layer = 0; layer < MAX_ALPHA_LAYERS; ++layer) {
        float remaining = maximum_distance >= INF * 0.5f ? INF : max(maximum_distance - travelled, 0.0f);
        if (remaining <= RAY_EPSILON) return hit;
        ray r(current_origin, direction, RAY_EPSILON, remaining);
        intersection_result<triangle_data> result = query.intersect(r, acceleration_structure);
        if (result.type == intersection_type::none || result.primitive_id >= uint(triangle_count)) return hit;

        Triangle triangle = triangles[result.primitive_id];
        float2 uv = triangleUv(triangle, result.triangle_barycentric_coord);
        uint material = as_type<uint>(triangle.p0.w);
        uint entity = as_type<uint>(triangle.p1.w);
        if ((apply_visibility && !visibility_all && entity_visibility[entity] == 0u) ||
            (alpha_cutouts && !alphaCutoutPass(material, uv, materials, uniforms, textures, material_sampler)))
        {
            float step = result.distance + RAY_EPSILON * 2.0f;
            travelled += step;
            current_origin += direction * step;
            continue;
        }

        float w0 = 1.0f - result.triangle_barycentric_coord.x - result.triangle_barycentric_coord.y;
        float3 barycentric = float3(w0, result.triangle_barycentric_coord.x, result.triangle_barycentric_coord.y);
        hit.found = true;
        hit.distance = travelled + result.distance;
        hit.position = origin + direction * hit.distance;
        hit.normal = normalize(triangle.n0.xyz * barycentric.x + triangle.n1.xyz * barycentric.y + triangle.n2.xyz * barycentric.z);
        hit.geometric_normal = normalize(cross(triangle.p1.xyz - triangle.p0.xyz, triangle.p2.xyz - triangle.p0.xyz));
        if (dot(hit.geometric_normal, direction) > 0.0f) hit.geometric_normal = -hit.geometric_normal;
        if (dot(hit.normal, hit.geometric_normal) < 0.0f) hit.normal = -hit.normal;
        hit.uv = uv; hit.material = material; hit.triangle = result.primitive_id;
        return hit;
    }
    return hit;
}

bool traceAnyAlpha(
    float3 origin,
    float3 direction,
    float maximum_distance,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    bool alpha_cutouts = (uniforms.counts.w & 1) != 0;
    if (!alpha_cutouts) {
        intersector<> query;
        query.assume_geometry_type(geometry_type::triangle);
        query.assume_identity_transforms(true);
        query.accept_any_intersection(true);
        ray r(origin, direction, RAY_EPSILON, maximum_distance);
        return query.intersect(r, acceleration_structure).type != intersection_type::none;
    }
    Hit hit = traceClosestAlpha(origin, direction, maximum_distance, false, acceleration_structure,
        triangles, materials, entity_visibility, uniforms, textures, material_sampler);
    return hit.found && hit.distance < maximum_distance;
}

float2 environmentUv(float3 direction, device const float4 *gi_data)
{
    direction = normalize(direction);
    float angle = gi_data[7].w;
    float c = cos(angle), s = sin(angle);
    float2 rotated = float2(c * direction.x - s * direction.z, s * direction.x + c * direction.z);
    direction.x = rotated.x; direction.z = rotated.y;
    return float2(0.5f + atan2(direction.z, direction.x) / (2.0f * PI),
        0.5f - asin(clamp(direction.y, -1.0f, 1.0f)) / PI);
}

float3 environmentRadiance(
    float3 direction,
    float roughness,
    device const float4 *gi_data,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    float3 average = max(gi_data[3].rgb, float3(0.0f));
    float intensity = max(gi_data[3].a, 0.0f);
    if (gi_data[4].a < 0.5f) return average * intensity;
    float3 sampled = pow(max(sampleTextureSlot(0, environmentUv(direction, gi_data), textures, material_sampler).rgb,
        float3(0.0f)), float3(2.2f));
    return mix(sampled, average, clamp(roughness * roughness, 0.0f, 1.0f)) * intensity;
}

float3 environmentBackground(
    float3 direction,
    device const float4 *gi_data,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    float3 sky = max(gi_data[4].rgb, float3(0.0f));
    float intensity = max(gi_data[3].a, 0.0f);
    if (gi_data[4].a < 0.5f) return sky * intensity;
    return pow(max(sampleTextureSlot(0, environmentUv(direction, gi_data), textures, material_sampler).rgb,
        float3(0.0f)), float3(2.2f)) * intensity;
}

float3 applyEnvironmentFog(float3 color, float distance_to_camera, device const float4 *gi_data)
{
    int mode = int(gi_data[5].a + 0.5f);
    if (mode == 0) return color;
    float visibility = 1.0f;
    if (mode == 1) {
        visibility = clamp((gi_data[7].z - distance_to_camera) /
            max(gi_data[7].z - gi_data[7].y, 1.0e-5f), 0.0f, 1.0f);
    } else if (mode == 2) {
        visibility = exp(-max(gi_data[7].x, 0.0f) * distance_to_camera);
    }
    return mix(max(gi_data[5].rgb, float3(0.0f)), color, visibility);
}

float3 tangentNormal(
    Hit hit,
    Material material,
    device const Triangle *triangles,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    int slot = packedTextureSlot(material, 0);
    if (slot < 0 || slot >= 32) return hit.normal;
    Triangle triangle = triangles[hit.triangle];
    float3 edge1 = triangle.p1.xyz - triangle.p0.xyz;
    float3 edge2 = triangle.p2.xyz - triangle.p0.xyz;
    float2 uv0 = triangle.uv01.xy, uv1 = triangle.uv01.zw, uv2 = triangle.uv2.xy;
    float2 duv1 = uv1 - uv0, duv2 = uv2 - uv0;
    float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(determinant) <= 1.0e-8f) return hit.normal;
    float3 n = normalize(hit.normal);
    float3 tangent = (edge1 * duv2.y - edge2 * duv1.y) / determinant;
    tangent -= n * dot(n, tangent);
    if (dot(tangent, tangent) <= 1.0e-10f) return n;
    tangent = normalize(tangent);
    float3 bitangent = normalize(cross(n, tangent));
    if (determinant < 0.0f) bitangent = -bitangent;
    float3 mapped = sampleTextureSlot(slot, hit.uv, textures, material_sampler).xyz * 2.0f - 1.0f;
    return dot(mapped, mapped) <= 1.0e-10f ? n : normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);
}

Surface surfaceAt(
    Hit hit,
    device const Triangle *triangles,
    device const Material *materials,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    Surface surface;
    Material material = hit.material < uint(max(uniforms.counts.z, 0)) ? materials[hit.material] : materials[0];
    surface.albedo = max(material.base_color.rgb, float3(0.0f));
    if (material.data.x >= 0 && material.data.x < 32)
        surface.albedo *= pow(max(sampleTextureSlot(material.data.x, hit.uv, textures, material_sampler).rgb,
            float3(0.0f)), float3(2.2f));
    surface.roughness = max(packedParameter(material, 0), 0.04f);
    surface.metallic = packedParameter(material, 1);
    surface.ao = packedParameter(material, 2);
    surface.clearcoat = packedParameter(material, 3);
    int roughness_slot = packedTextureSlot(material, 1);
    int metallic_slot = packedTextureSlot(material, 2);
    int ao_slot = packedTextureSlot(material, 3);
    int emissive_slot = packedTextureSlot(material, 4);
    if (roughness_slot >= 0 && roughness_slot < 32)
        surface.roughness = max(surface.roughness * sampleTextureSlot(roughness_slot, hit.uv, textures, material_sampler).r, 0.04f);
    if (metallic_slot >= 0 && metallic_slot < 32)
        surface.metallic *= sampleTextureSlot(metallic_slot, hit.uv, textures, material_sampler).r;
    if (ao_slot >= 0 && ao_slot < 32)
        surface.ao *= sampleTextureSlot(ao_slot, hit.uv, textures, material_sampler).r;
    surface.metallic = clamp(surface.metallic, 0.0f, 1.0f);
    surface.ao = clamp(surface.ao, 0.0f, 1.0f);
    surface.normal = tangentNormal(hit, material, triangles, textures, material_sampler);
    surface.emissive = packedEmissiveColor(material) * packedEmissiveStrength(material);
    if (emissive_slot >= 0 && emissive_slot < 32)
        surface.emissive *= pow(max(sampleTextureSlot(emissive_slot, hit.uv, textures, material_sampler).rgb,
            float3(0.0f)), float3(2.2f));
    return surface;
}

float distributionGGX(float3 n, float3 h, float roughness)
{
    float a = roughness * roughness, a2 = a * a;
    float nh = max(dot(n, h), 0.0f);
    float denominator = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denominator * denominator, 1.0e-6f);
}

float geometrySchlickGGX(float nv, float roughness)
{
    float r = roughness + 1.0f, k = r * r * 0.125f;
    return nv / max(nv * (1.0f - k) + k, 1.0e-6f);
}

float geometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    return geometrySchlickGGX(max(dot(n, v), 0.0f), roughness) *
        geometrySchlickGGX(max(dot(n, l), 0.0f), roughness);
}

float3 fresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0f - f0) * pow(clamp(1.0f - cosine, 0.0f, 1.0f), 5.0f);
}

float3 directBrdf(Surface surface, float3 view_direction, float3 light_direction, float3 incoming)
{
    float3 n = normalize(surface.normal), v = normalize(view_direction), l = normalize(light_direction);
    float3 h = normalize(v + l);
    float nl = max(dot(n, l), 0.0f), nv = max(dot(n, v), 0.0f);
    if (nl <= 0.0f || nv <= 0.0f) return float3(0.0f);
    float3 f0 = mix(float3(0.04f), surface.albedo, surface.metallic);
    float3 f = fresnelSchlick(max(dot(h, v), 0.0f), f0);
    float d = distributionGGX(n, h, surface.roughness);
    float g = geometrySmith(n, v, l, surface.roughness);
    float3 specular = d * g * f / max(4.0f * nv * nl, 1.0e-5f);
    float3 diffuse = (float3(1.0f) - f) * (1.0f - surface.metallic) * surface.albedo / PI;
    if (surface.clearcoat > 0.0f) {
        float coat_d = distributionGGX(n, h, max(surface.roughness * 0.35f, 0.04f));
        float coat_g = geometrySmith(n, v, l, 0.25f);
        float3 coat_f = fresnelSchlick(max(dot(h, v), 0.0f), float3(0.04f));
        specular += surface.clearcoat * coat_d * coat_g * coat_f / max(4.0f * nv * nl, 1.0e-5f);
    }
    return (diffuse + specular) * incoming * nl;
}

int giProbeIndex(int3 coordinate, int3 dimensions)
{
    return coordinate.x + dimensions.x * (coordinate.y + dimensions.y * coordinate.z);
}

float3 giCoefficient(device const float4 *gi_data, int3 coordinate, int3 dimensions, int coefficient)
{
    return gi_data[GI_HEADER_VEC4S + giProbeIndex(coordinate, dimensions) * 4 + coefficient].xyz;
}

float3 sampleGlobalIllumination(device const float4 *gi_data, float3 position, float3 normal)
{
    if (gi_data[0].w < 0.5f) return float3(0.0f);
    float3 minimum = gi_data[0].xyz, maximum = gi_data[1].xyz;
    float intensity = max(gi_data[1].w, 0.0f);
    int3 dimensions = max(int3(gi_data[2].xyz + float3(0.5f)), int3(2));
    float3 extent = max(maximum - minimum, float3(1.0e-6f));
    float3 grid = clamp((position - minimum) / extent, float3(0.0f), float3(1.0f)) * float3(dimensions - 1);
    int3 i0 = int3(floor(grid));
    int3 i1 = min(i0 + int3(1), dimensions - int3(1));
    float3 t = grid - float3(i0);
    float3 coefficients[4];
    for (int coefficient = 0; coefficient < 4; ++coefficient) {
        float3 c000 = giCoefficient(gi_data, int3(i0.x, i0.y, i0.z), dimensions, coefficient);
        float3 c100 = giCoefficient(gi_data, int3(i1.x, i0.y, i0.z), dimensions, coefficient);
        float3 c010 = giCoefficient(gi_data, int3(i0.x, i1.y, i0.z), dimensions, coefficient);
        float3 c110 = giCoefficient(gi_data, int3(i1.x, i1.y, i0.z), dimensions, coefficient);
        float3 c001 = giCoefficient(gi_data, int3(i0.x, i0.y, i1.z), dimensions, coefficient);
        float3 c101 = giCoefficient(gi_data, int3(i1.x, i0.y, i1.z), dimensions, coefficient);
        float3 c011 = giCoefficient(gi_data, int3(i0.x, i1.y, i1.z), dimensions, coefficient);
        float3 c111 = giCoefficient(gi_data, int3(i1.x, i1.y, i1.z), dimensions, coefficient);
        float3 c00 = mix(c000, c100, t.x), c10 = mix(c010, c110, t.x);
        float3 c01 = mix(c001, c101, t.x), c11 = mix(c011, c111, t.x);
        coefficients[coefficient] = mix(mix(c00, c10, t.y), mix(c01, c11, t.y), t.z);
    }
    float3 n = normalize(normal);
    float3 irradiance = coefficients[0] * (PI * SH_Y00);
    irradiance += coefficients[1] * ((2.0f * PI / 3.0f) * SH_Y1 * n.x);
    irradiance += coefficients[2] * ((2.0f * PI / 3.0f) * SH_Y1 * n.y);
    irradiance += coefficients[3] * ((2.0f * PI / 3.0f) * SH_Y1 * n.z);
    return max(irradiance, float3(0.0f)) * intensity;
}

float localLightAttenuation(float distance_to_light, device const float4 *gi_data)
{
    float range = gi_data[10].w;
    if (range <= 0.0f) return 1.0f;
    float ratio = clamp(distance_to_light / range, 0.0f, 1.0f);
    float falloff = 1.0f - ratio * ratio;
    return falloff * falloff;
}

float spotAttenuation(float3 light_to_surface, device const float4 *gi_data)
{
    float cosine = dot(normalize(gi_data[9].xyz), normalize(light_to_surface));
    return clamp((cosine - gi_data[11].y) / max(gi_data[11].x - gi_data[11].y, 1.0e-5f), 0.0f, 1.0f);
}

float3 shade(
    float3 origin,
    float3 direction,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const float4 *gi_data,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler,
    thread float& depth)
{
    Hit hit = traceClosestAlpha(origin, direction, INF, true, acceleration_structure, triangles, materials,
        entity_visibility, uniforms, textures, material_sampler);
    if (!hit.found) {
        depth = INF;
        return environmentBackground(direction, gi_data, textures, material_sampler);
    }

    depth = max(dot(hit.position - uniforms.camera_position.xyz, normalize(uniforms.camera_forward.xyz)), RAY_EPSILON);
    Surface surface = surfaceAt(hit, triangles, materials, uniforms, textures, material_sampler);
    float3 radiance = surface.emissive;
    float3 view_direction = -direction;
    int light_type = int(gi_data[9].w + 0.5f);
    float light_intensity = max(gi_data[8].w, 0.0f);

    if (light_type != 0 && light_intensity > 0.0f) {
        float3 light_direction;
        float3 incoming = max(gi_data[10].rgb, float3(0.0f)) * light_intensity;
        float shadow_distance = INF;
        if (light_type == 1 || light_type == 3) {
            float3 to_light = gi_data[8].xyz - hit.position;
            float distance_squared = max(dot(to_light, to_light), 1.0e-4f);
            float light_distance = sqrt(distance_squared);
            light_direction = to_light / light_distance;
            incoming *= localLightAttenuation(light_distance, gi_data) / distance_squared;
            shadow_distance = max(light_distance - RAY_EPSILON * 8.0f, 0.0f);
            if (light_type == 3) incoming *= spotAttenuation(hit.position - gi_data[8].xyz, gi_data);
        } else {
            light_direction = normalize(-gi_data[9].xyz);
        }
        if (max(dot(surface.normal, light_direction), 0.0f) > 0.0f) {
            float3 shadow_origin = hit.position + hit.geometric_normal * RAY_EPSILON * 4.0f;
            if (!traceAnyAlpha(shadow_origin, light_direction, shadow_distance, acceleration_structure,
                triangles, materials, entity_visibility, uniforms, textures, material_sampler))
            {
                radiance += directBrdf(surface, view_direction, light_direction, incoming);
            }
        }
    }

    float3 indirect = sampleGlobalIllumination(gi_data, hit.position, surface.normal);
    radiance += surface.albedo * (1.0f - surface.metallic) * surface.ao * indirect / PI;
    radiance += max(gi_data[6].rgb, float3(0.0f)) * max(gi_data[6].a, 0.0f) * surface.albedo * surface.ao;
    float3 f0 = mix(float3(0.04f), surface.albedo, surface.metallic);
    float3 f = fresnelSchlick(max(dot(surface.normal, view_direction), 0.0f), f0);
    float3 reflected = reflect(-view_direction, surface.normal);
    radiance += environmentRadiance(reflected, surface.roughness, gi_data, textures, material_sampler) * f * surface.ao;
    radiance += max(gi_data[3].rgb, float3(0.0f)) * max(gi_data[3].a, 0.0f) *
        surface.albedo * (1.0f - surface.metallic) * surface.ao / PI;
    return applyEnvironmentFog(max(radiance, float3(0.0f)), hit.distance, gi_data);
}

float deterministicDepth(
    float2 sample_pixel,
    uint2 size,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    float2 uv = sample_pixel / float2(size);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float3 direction = normalize(uniforms.camera_forward.xyz +
        uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +
        uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w));
    Hit hit = traceClosestAlpha(uniforms.camera_position.xyz, direction, INF, true, acceleration_structure,
        triangles, materials, entity_visibility, uniforms, textures, material_sampler);
    if (!hit.found) return INF;
    return max(dot(hit.position - uniforms.camera_position.xyz, normalize(uniforms.camera_forward.xyz)), RAY_EPSILON);
}

kernel void raytrace_kernel(
    device const Triangle *triangles [[buffer(1)]],
    device const Material *materials [[buffer(2)]],
    constant TraceUniforms& uniforms [[buffer(3)]],
    device const float4 *gi_data [[buffer(4)]],
    primitive_acceleration_structure acceleration_structure [[buffer(5)]],
    device const uint *entity_visibility [[buffer(6)]],
    texture2d<float, access::write> output_texture [[texture(0)]],
    array<texture2d<float>, 32> textures [[texture(1)]],
    texture2d<float, access::write> primary_depth [[texture(33)]],
    sampler material_sampler [[sampler(0)]],
    uint2 pixel [[thread_position_in_grid]])
{
    uint2 size = uint2(uint(max(uniforms.resolution_aspect.x, 1.0f)), uint(max(uniforms.resolution_aspect.y, 1.0f)));
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    float2 uv = (float2(pixel) + float2(0.5f)) / float2(size);
    float2 ndc = uv * 2.0f - 1.0f; ndc.y = -ndc.y;
    float3 direction = normalize(uniforms.camera_forward.xyz +
        uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +
        uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w));
    float depth = INF;
    float3 radiance = shade(uniforms.camera_position.xyz, direction, acceleration_structure, triangles,
        materials, gi_data, entity_visibility, uniforms, textures, material_sampler, depth);
    output_texture.write(float4(radiance, 1.0f), pixel);
    primary_depth.write(float4(depth, 0.0f, 0.0f, 1.0f), pixel);
}

kernel void trace_kernel(
    device const Triangle *triangles [[buffer(1)]],
    device const Material *materials [[buffer(2)]],
    constant TraceUniforms& uniforms [[buffer(3)]],
    device const float4 *gi_data [[buffer(4)]],
    primitive_acceleration_structure acceleration_structure [[buffer(5)]],
    device const uint *entity_visibility [[buffer(6)]],
    texture2d<float, access::read_write> accumulation [[texture(0)]],
    array<texture2d<float>, 32> textures [[texture(1)]],
    texture2d<float, access::write> primary_depth [[texture(33)]],
    sampler material_sampler [[sampler(0)]],
    uint2 pixel [[thread_position_in_grid]])
{
    uint2 size = uint2(uint(max(uniforms.resolution_aspect.x, 1.0f)), uint(max(uniforms.resolution_aspect.y, 1.0f)));
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    bool reset = uniforms.frame.y != 0u;
    bool moving = uniforms.frame.w != 0u;
    if (reset) accumulation.write(float4(0.0f), pixel);

    if (moving) {
        uint block = max(uniforms.path_policy.w, 1u);
        if ((pixel.x % block) == 0u && (pixel.y % block) == 0u) {
            float2 sample_pixel = min(float2(pixel) + float2(float(block) * 0.5f), float2(size) - float2(0.5f));
            float d = deterministicDepth(sample_pixel, size, acceleration_structure, triangles, materials,
                entity_visibility, uniforms, textures, material_sampler);
            uint2 end = min(pixel + uint2(block), size);
            for (uint y = pixel.y; y < end.y; ++y)
                for (uint x = pixel.x; x < end.x; ++x)
                    primary_depth.write(float4(d, 0.0f, 0.0f, 1.0f), uint2(x, y));
        }
    } else if (reset) {
        float d = deterministicDepth(float2(pixel) + float2(0.5f), size, acceleration_structure, triangles,
            materials, entity_visibility, uniforms, textures, material_sampler);
        primary_depth.write(float4(d, 0.0f, 0.0f, 1.0f), pixel);
    }

    uint phase_grid = moving ? max(uniforms.path_policy.z, 1u) :
        (reset ? max(uniforms.path_policy.y, 1u) : max(uniforms.path_policy.x, 1u));
    uint phase_count = phase_grid * phase_grid;
    uint phase = uniforms.frame.x % phase_count;
    uint phase_x = phase % phase_grid, phase_y = phase / phase_grid;
    uint pixel_phase = (pixel.x % phase_grid) + (pixel.y % phase_grid) * phase_grid;
    if (pixel_phase != phase) return;

    uint sample_count = uint(max(uniforms.counts.x, 1));
    float3 sample_radiance = float3(0.0f);
    for (uint sample = 0u; sample < sample_count; ++sample) {
        uint state = hashUint(pixel.x * 1973u ^ pixel.y * 9277u ^ uniforms.frame.x * 26699u ^
            sample * 31847u ^ (phase_x + phase_y * phase_grid) * 104729u ^ 0x68bc21ebu);
        float2 jitter = float2(randomFloat(state), randomFloat(state)) - 0.5f;
        float2 uv = (float2(pixel) + float2(0.5f) + jitter) / float2(size);
        float2 ndc = uv * 2.0f - 1.0f; ndc.y = -ndc.y;
        float3 direction = normalize(uniforms.camera_forward.xyz +
            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +
            uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w));
        float depth = INF;
        sample_radiance += shade(uniforms.camera_position.xyz, direction, acceleration_structure, triangles,
            materials, gi_data, entity_visibility, uniforms, textures, material_sampler, depth);
    }
    float4 previous = reset ? float4(0.0f) : accumulation.read(pixel);
    accumulation.write(float4(previous.rgb + sample_radiance, previous.a + float(sample_count)), pixel);
}

float4 reconstructSparseSample(texture2d<float, access::read> accumulation, int2 pixel, int2 size, int phase_grid, int phase)
{
    pixel = clamp(pixel, int2(0), size - int2(1));
    int grid = max(phase_grid, 1);
    int current_phase = clamp(phase, 0, max(grid * grid - 1, 0));
    int2 phase_offset = int2(current_phase % grid, current_phase / grid);
    int2 max_cell = max((size - int2(1) - phase_offset) / grid, int2(0));
    float2 lattice = (float2(pixel) - float2(phase_offset)) / float(grid);
    float2 g = clamp(lattice, float2(0.0f), float2(max_cell));
    int2 c0 = int2(floor(g)), c1 = min(c0 + int2(1), max_cell);
    float2 t = g - float2(c0);
    int2 p00 = c0 * grid + phase_offset;
    int2 p10 = int2(c1.x, c0.y) * grid + phase_offset;
    int2 p01 = int2(c0.x, c1.y) * grid + phase_offset;
    int2 p11 = c1 * grid + phase_offset;
    float4 s00 = accumulation.read(uint2(p00)), s10 = accumulation.read(uint2(p10));
    float4 s01 = accumulation.read(uint2(p01)), s11 = accumulation.read(uint2(p11));
    float w00 = (1.0f - t.x) * (1.0f - t.y) * (s00.a > 0.0f ? 1.0f : 0.0f);
    float w10 = t.x * (1.0f - t.y) * (s10.a > 0.0f ? 1.0f : 0.0f);
    float w01 = (1.0f - t.x) * t.y * (s01.a > 0.0f ? 1.0f : 0.0f);
    float w11 = t.x * t.y * (s11.a > 0.0f ? 1.0f : 0.0f);
    float sum = w00 + w10 + w01 + w11;
    return sum <= 1.0e-6f ? s00 : (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) / sum;
}

kernel void resolve_pathtrace_kernel(
    constant ResolveUniforms& uniforms [[buffer(0)]],
    texture2d<float, access::read> accumulation [[texture(0)]],
    texture2d<float, access::write> resolved [[texture(1)]],
    uint2 pixel [[thread_position_in_grid]])
{
    uint2 size = uint2(resolved.get_width(), resolved.get_height());
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int2 ipixel = int2(pixel);
    int2 isize = int2(size);
    float4 value = uniforms.params.x != 0u
        ? reconstructSparseSample(
            accumulation,
            ipixel,
            isize,
            int(max(uniforms.params.y, 1u)),
            int(uniforms.params.z))
        : accumulation.read(pixel);
    float samples = max(value.a, 1.0f);
    resolved.write(float4(max(value.rgb / samples, float3(0.0f)), 1.0f), pixel);
}

)MSL";

} // namespace Renderer::PbrMetalShaders

#endif
