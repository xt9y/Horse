#ifndef RW_ENGINE_RENDERER_PATHTRACER_METAL_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_METAL_SHADERS_HPP

namespace Renderer::PathTracerMetalShaders {

inline constexpr const char *source = R"MSL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace raytracing;

struct Triangle {
    float4 p0;
    float4 p1;
    float4 p2;
    float4 n0;
    float4 n1;
    float4 n2;
    float4 uv01;
    float4 uv2;
};

struct Material {
    float4 base_color;
    int4 data;
};

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

struct PresentUniforms {
    float exposure;
    float camera_moving;
    float phase_grid;
    float phase;
};

struct Hit {
    bool found;
    float distance;
    float3 position;
    float3 normal;
    float3 geometric_normal;
    float2 uv;
    uint material;
};

struct PresentOut {
    float4 position [[position]];
    float2 uv;
};

constant float PI = 3.14159265358979323846f;
constant float RAY_EPSILON = 0.0025f;
constant float INF = 1.0e30f;
constant float SH_Y00 = 0.2820947918f;
constant float SH_Y1 = 0.4886025119f;
constant int GI_HEADER_VEC4S = 4;
constant int MAX_ALPHA_LAYERS = 16;

uint hashUint(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
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
    float2 coordinates = float2(uv.x, 1.0f - uv.y);
    switch (slot) {
        case 0: return textures[0].sample(material_sampler, coordinates);
        case 1: return textures[1].sample(material_sampler, coordinates);
        case 2: return textures[2].sample(material_sampler, coordinates);
        case 3: return textures[3].sample(material_sampler, coordinates);
        case 4: return textures[4].sample(material_sampler, coordinates);
        case 5: return textures[5].sample(material_sampler, coordinates);
        case 6: return textures[6].sample(material_sampler, coordinates);
        case 7: return textures[7].sample(material_sampler, coordinates);
        case 8: return textures[8].sample(material_sampler, coordinates);
        case 9: return textures[9].sample(material_sampler, coordinates);
        case 10: return textures[10].sample(material_sampler, coordinates);
        case 11: return textures[11].sample(material_sampler, coordinates);
        case 12: return textures[12].sample(material_sampler, coordinates);
        case 13: return textures[13].sample(material_sampler, coordinates);
        case 14: return textures[14].sample(material_sampler, coordinates);
        case 15: return textures[15].sample(material_sampler, coordinates);
        case 16: return textures[16].sample(material_sampler, coordinates);
        case 17: return textures[17].sample(material_sampler, coordinates);
        case 18: return textures[18].sample(material_sampler, coordinates);
        case 19: return textures[19].sample(material_sampler, coordinates);
        case 20: return textures[20].sample(material_sampler, coordinates);
        case 21: return textures[21].sample(material_sampler, coordinates);
        case 22: return textures[22].sample(material_sampler, coordinates);
        case 23: return textures[23].sample(material_sampler, coordinates);
        case 24: return textures[24].sample(material_sampler, coordinates);
        case 25: return textures[25].sample(material_sampler, coordinates);
        case 26: return textures[26].sample(material_sampler, coordinates);
        case 27: return textures[27].sample(material_sampler, coordinates);
        case 28: return textures[28].sample(material_sampler, coordinates);
        case 29: return textures[29].sample(material_sampler, coordinates);
        case 30: return textures[30].sample(material_sampler, coordinates);
        case 31: return textures[31].sample(material_sampler, coordinates);
        default: return float4(1.0f);
    }
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
    const int material_count = max(uniforms.counts.z, 0);
    if (material_index >= uint(material_count)) return true;
    Material material = materials[material_index];
    float alpha = clamp(material.base_color.a, 0.0f, 1.0f);
    int slot = material.data.x;
    if (slot >= 0 && slot < 32) {
        float4 texel = sampleTextureSlot(slot, uv, textures, material_sampler);
        alpha *= texel.a;
    }
    return alpha >= uniforms.resolution_aspect.w;
}

Hit traceClosestAlpha(
    float3 origin,
    float3 direction,
    float max_distance,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    Hit hit;
    hit.found = false;
    hit.distance = max_distance;
    hit.position = float3(0.0f);
    hit.normal = float3(0.0f, 1.0f, 0.0f);
    hit.geometric_normal = float3(0.0f, 1.0f, 0.0f);
    hit.uv = float2(0.0f);
    hit.material = 0u;

    const int triangle_count = max(uniforms.counts.y, 0);
    if (triangle_count <= 0) return hit;

    const bool alpha_cutouts = (uniforms.counts.w & 1) != 0;
    const bool visibility_all = (uniforms.counts.w & 2) != 0;
    intersector<triangle_data> triangle_intersector;
    triangle_intersector.assume_geometry_type(geometry_type::triangle);
    triangle_intersector.assume_identity_transforms(true);
    float3 current_origin = origin;
    float travelled = 0.0f;

    for (int layer = 0; layer < MAX_ALPHA_LAYERS; ++layer) {
        float remaining = max_distance >= INF * 0.5f
            ? INF
            : max(max_distance - travelled, 0.0f);
        if (remaining <= RAY_EPSILON) return hit;

        ray r(current_origin, direction, RAY_EPSILON, remaining);
        intersection_result<triangle_data> result = triangle_intersector.intersect(r, acceleration_structure);
        if (result.type == intersection_type::none || result.primitive_id >= uint(triangle_count)) return hit;

        Triangle triangle = triangles[result.primitive_id];
        float2 uv = triangleUv(triangle, result.triangle_barycentric_coord);
        uint material = as_type<uint>(triangle.p0.w);
        uint entity = as_type<uint>(triangle.p1.w);
        if ((!visibility_all && entity_visibility[entity] == 0u) ||
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
        hit.uv = uv;
        hit.material = material;
        return hit;
    }
    return hit;
}

bool traceAnyAlpha(
    float3 origin,
    float3 direction,
    float max_distance,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    const bool alpha_cutouts = (uniforms.counts.w & 1) != 0;
    const bool visibility_all = (uniforms.counts.w & 2) != 0;
    if (!alpha_cutouts && visibility_all) {
        intersector<> shadow_intersector;
        shadow_intersector.assume_geometry_type(geometry_type::triangle);
        shadow_intersector.assume_identity_transforms(true);
        shadow_intersector.accept_any_intersection(true);
        ray r(origin, direction, RAY_EPSILON, max_distance);
        intersection_result<> result = shadow_intersector.intersect(r, acceleration_structure);
        return result.type != intersection_type::none;
    }
    Hit hit = traceClosestAlpha(
        origin,
        direction,
        max_distance,
        acceleration_structure,
        triangles,
        materials,
        entity_visibility,
        uniforms,
        textures,
        material_sampler
    );
    return hit.found && hit.distance < max_distance;
}

float deterministicDepthAlpha(
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
    float2 depth_uv = sample_pixel / float2(size);
    float2 depth_ndc = depth_uv * 2.0f - 1.0f;
    depth_ndc.y = -depth_ndc.y;
    float3 depth_direction = normalize(
        uniforms.camera_forward.xyz +
        uniforms.camera_right.xyz * (
            depth_ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w
        ) +
        uniforms.camera_up.xyz * (depth_ndc.y * uniforms.light_color_tan_half_fov.w)
    );
    Hit depth_hit = traceClosestAlpha(
        uniforms.camera_position.xyz,
        depth_direction,
        INF,
        acceleration_structure,
        triangles,
        materials,
        entity_visibility,
        uniforms,
        textures,
        material_sampler
    );
    if (!depth_hit.found) return INF;
    return max(
        dot(
            depth_hit.position - uniforms.camera_position.xyz,
            normalize(uniforms.camera_forward.xyz)
        ),
        RAY_EPSILON
    );
}

float3 materialAlbedo(
    uint material_index,
    float2 uv,
    device const Material *materials,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    const int material_count = max(uniforms.counts.z, 0);
    if (material_index >= uint(material_count)) return float3(1.0f);
    Material material = materials[material_index];
    float3 albedo = max(material.base_color.rgb, float3(0.0f));
    int slot = material.data.x;
    if (slot >= 0 && slot < 32) {
        float4 texel = sampleTextureSlot(slot, uv, textures, material_sampler);
        albedo *= pow(max(texel.rgb, float3(0.0f)), float3(2.2f));
    }
    return clamp(albedo, float3(0.0f), float3(1.0f));
}

int giProbeIndex(int3 coordinate, int3 dimensions)
{
    return coordinate.x + dimensions.x * (coordinate.y + dimensions.y * coordinate.z);
}

float3 giCoefficient(
    device const float4 *gi_data,
    int3 coordinate,
    int3 dimensions,
    int coefficient)
{
    int probe = giProbeIndex(coordinate, dimensions);
    return gi_data[GI_HEADER_VEC4S + probe * 4 + coefficient].xyz;
}

float3 sampleGlobalIllumination(
    device const float4 *gi_data,
    float3 position,
    float3 normal)
{
    if (gi_data[0].w < 0.5f) return float3(0.0f);

    float3 minimum = gi_data[0].xyz;
    float3 maximum = gi_data[1].xyz;
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
        float3 c00 = mix(c000, c100, t.x);
        float3 c10 = mix(c010, c110, t.x);
        float3 c01 = mix(c001, c101, t.x);
        float3 c11 = mix(c011, c111, t.x);
        coefficients[coefficient] = mix(mix(c00, c10, t.y), mix(c01, c11, t.y), t.z);
    }

    float3 n = normalize(normal);
    float3 irradiance = coefficients[0] * (PI * SH_Y00);
    irradiance += coefficients[1] * ((2.0f * PI / 3.0f) * SH_Y1 * n.x);
    irradiance += coefficients[2] * ((2.0f * PI / 3.0f) * SH_Y1 * n.y);
    irradiance += coefficients[3] * ((2.0f * PI / 3.0f) * SH_Y1 * n.z);
    return max(irradiance, float3(0.0f)) * intensity;
}

float3 tracePath(
    float3 origin,
    float3 direction,
    primitive_acceleration_structure acceleration_structure,
    device const Triangle *triangles,
    device const Material *materials,
    device const float4 *gi_data,
    device const uint *entity_visibility,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 32> textures,
    sampler material_sampler)
{
    Hit hit = traceClosestAlpha(
        origin,
        direction,
        INF,
        acceleration_structure,
        triangles,
        materials,
        entity_visibility,
        uniforms,
        textures,
        material_sampler
    );
    if (!hit.found) return float3(0.0f);

    float3 albedo = materialAlbedo(hit.material, hit.uv, materials, uniforms, textures, material_sampler);
    float3 radiance = float3(0.0f);

    if (uniforms.frame.z != 0u && uniforms.light_position_intensity.w > 0.0f) {
        float3 to_light = uniforms.light_position_intensity.xyz - hit.position;
        float distance_squared = max(dot(to_light, to_light), 1.0e-4f);
        float light_distance = sqrt(distance_squared);
        float3 light_direction = to_light / light_distance;
        float cosine = max(dot(hit.normal, light_direction), 0.0f);

        if (cosine > 0.0f) {
            float3 shadow_origin = hit.position + hit.geometric_normal * RAY_EPSILON * 4.0f;
            float shadow_distance = max(light_distance - RAY_EPSILON * 8.0f, 0.0f);
            if (!traceAnyAlpha(
                shadow_origin,
                light_direction,
                shadow_distance,
                acceleration_structure,
                triangles,
                materials,
                entity_visibility,
                uniforms,
                textures,
                material_sampler
            )) {
                float3 incoming = max(uniforms.light_color_tan_half_fov.xyz, float3(0.0f)) *
                    max(uniforms.light_position_intensity.w, 0.0f) / distance_squared;
                radiance += albedo * incoming * (cosine / PI);
            }
        }
    }

    float3 indirect_irradiance = sampleGlobalIllumination(gi_data, hit.position, hit.normal);
    radiance += albedo * indirect_irradiance * (1.0f / PI);
    return radiance;
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

    const bool reset = uniforms.frame.y != 0u;
    const bool camera_moving = uniforms.frame.w != 0u;
    if (reset) accumulation.write(float4(0.0f), pixel);

    if (camera_moving) {
        const uint moving_depth_block = max(uniforms.path_policy.w, 1u);
        if ((pixel.x % moving_depth_block) == 0u && (pixel.y % moving_depth_block) == 0u) {
            float2 sample_pixel = min(
                float2(pixel) + float2(float(moving_depth_block) * 0.5f),
                float2(size) - float2(0.5f)
            );
            float depth_value = deterministicDepthAlpha(
                sample_pixel,
                size,
                acceleration_structure,
                triangles,
                materials,
                entity_visibility,
                uniforms,
                textures,
                material_sampler
            );
            uint2 block_end = min(pixel + uint2(moving_depth_block), size);
            for (uint y = pixel.y; y < block_end.y; ++y) {
                for (uint x = pixel.x; x < block_end.x; ++x) {
                    uint2 target = uint2(x, y);
                    primary_depth.write(float4(depth_value, 0.0f, 0.0f, 1.0f), target);
                }
            }
        }
    } else if (reset) {
        float depth_value = deterministicDepthAlpha(
            float2(pixel) + float2(0.5f),
            size,
            acceleration_structure,
            triangles,
            materials,
            entity_visibility,
            uniforms,
            textures,
            material_sampler
        );
        primary_depth.write(float4(depth_value, 0.0f, 0.0f, 1.0f), pixel);
    }

    uint phase_grid = camera_moving ? max(uniforms.path_policy.z, 1u) :
        (reset ? max(uniforms.path_policy.y, 1u) : max(uniforms.path_policy.x, 1u));
    uint phase_count = phase_grid * phase_grid;
    uint phase = uniforms.frame.x % phase_count;
    uint phase_x = phase % phase_grid;
    uint phase_y = phase / phase_grid;
    uint pixel_phase = (pixel.x % phase_grid) + (pixel.y % phase_grid) * phase_grid;
    if (pixel_phase != phase) return;

    uint sample_count = uint(max(uniforms.counts.x, 1));
    float3 sample_radiance = float3(0.0f);
    for (uint sample = 0u; sample < sample_count; ++sample) {
        uint state = hashUint(
            pixel.x * 1973u ^
            pixel.y * 9277u ^
            uniforms.frame.x * 26699u ^
            sample * 31847u ^
            (phase_x + phase_y * phase_grid) * 104729u ^
            0x68bc21ebu
        );

        float2 jitter = float2(randomFloat(state), randomFloat(state)) - 0.5f;
        float2 uv = (float2(pixel) + float2(0.5f) + jitter) / float2(size);
        float2 ndc = uv * 2.0f - 1.0f;
        ndc.y = -ndc.y;
        float3 direction = normalize(
            uniforms.camera_forward.xyz +
            uniforms.camera_right.xyz * (ndc.x * uniforms.resolution_aspect.z * uniforms.light_color_tan_half_fov.w) +
            uniforms.camera_up.xyz * (ndc.y * uniforms.light_color_tan_half_fov.w)
        );

        sample_radiance += tracePath(
            uniforms.camera_position.xyz,
            direction,
            acceleration_structure,
            triangles,
            materials,
            gi_data,
            entity_visibility,
            uniforms,
            textures,
            material_sampler
        );
    }

    float4 previous = reset ? float4(0.0f) : accumulation.read(pixel);
    accumulation.write(float4(previous.rgb + sample_radiance, previous.a + float(sample_count)), pixel);
}

vertex PresentOut present_vertex(uint id [[vertex_id]])
{
    float2 position = id == 0u
        ? float2(-1.0f, -1.0f)
        : (id == 1u ? float2(3.0f, -1.0f) : float2(-1.0f, 3.0f));
    PresentOut out;
    out.position = float4(position, 0.0f, 1.0f);
    out.uv = position * 0.5f + 0.5f;
    return out;
}

float4 reconstructSparseSample(
    texture2d<float> accumulation,
    int2 pixel,
    int2 size,
    int phase_grid,
    int phase)
{
    pixel = clamp(pixel, int2(0), size - int2(1));
    int grid = max(phase_grid, 1);
    int phase_count = grid * grid;
    int current_phase = clamp(phase, 0, max(phase_count - 1, 0));
    int2 phase_offset = int2(current_phase % grid, current_phase / grid);
    int2 max_cell = max((size - int2(1) - phase_offset) / grid, int2(0));
    float2 lattice = (float2(pixel) - float2(phase_offset)) / float(grid);
    float2 lattice_grid = clamp(lattice, float2(0.0f), float2(max_cell));
    int2 cell0 = int2(floor(lattice_grid));
    int2 cell1 = min(cell0 + int2(1), max_cell);
    float2 t = lattice_grid - float2(cell0);

    int2 p00 = cell0 * grid + phase_offset;
    int2 p10 = int2(cell1.x, cell0.y) * grid + phase_offset;
    int2 p01 = int2(cell0.x, cell1.y) * grid + phase_offset;
    int2 p11 = cell1 * grid + phase_offset;

    float4 s00 = accumulation.read(uint2(p00));
    float4 s10 = accumulation.read(uint2(p10));
    float4 s01 = accumulation.read(uint2(p01));
    float4 s11 = accumulation.read(uint2(p11));

    float w00 = (1.0f - t.x) * (1.0f - t.y) * (s00.a > 0.0f ? 1.0f : 0.0f);
    float w10 = t.x * (1.0f - t.y) * (s10.a > 0.0f ? 1.0f : 0.0f);
    float w01 = (1.0f - t.x) * t.y * (s01.a > 0.0f ? 1.0f : 0.0f);
    float w11 = t.x * t.y * (s11.a > 0.0f ? 1.0f : 0.0f);
    float weight_sum = w00 + w10 + w01 + w11;
    if (weight_sum <= 1.0e-6f) return s00;
    return (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) / weight_sum;
}

fragment float4 present_fragment(
    PresentOut in [[stage_in]],
    constant PresentUniforms& uniforms [[buffer(0)]],
    texture2d<float> accumulation [[texture(0)]],
    sampler accumulation_sampler [[sampler(0)]])
{
    int2 size = int2(int(accumulation.get_width()), int(accumulation.get_height()));
    float2 sample_uv = float2(in.uv.x, 1.0f - in.uv.y);
    int2 pixel = clamp(int2(sample_uv * float2(size)), int2(0), size - int2(1));

    float4 accumulated;
    if (uniforms.camera_moving > 0.5f) {
        accumulated = reconstructSparseSample(
            accumulation,
            pixel,
            size,
            int(uniforms.phase_grid),
            int(uniforms.phase));
    } else {
        accumulated = accumulation.sample(accumulation_sampler, sample_uv);
    }

    float samples = max(accumulated.a, 1.0f);
    float3 linear_color = max(accumulated.rgb / samples, float3(0.0f));
    linear_color *= max(uniforms.exposure, 0.0f);
    float3 mapped = linear_color / (float3(1.0f) + linear_color);
    mapped = pow(mapped, float3(1.0f / 2.2f));
    return float4(mapped, 1.0f);
}
)MSL";

} // namespace Renderer::PathTracerMetalShaders

#endif
