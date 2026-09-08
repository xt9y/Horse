#ifndef RW_ENGINE_RENDERER_PATHTRACER_METAL_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_METAL_SHADERS_HPP

namespace Renderer::PathTracerMetalShaders {

inline constexpr const char *source = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct Node {
    packed_float3 bmin;
    uint first;
    packed_float3 bmax;
    uint meta;
    uint4 extra;
};

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
};

struct PresentUniforms {
    float4 exposure;
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

constant uint LEAF_BIT = 0x80000000u;
constant float PI = 3.14159265358979323846f;
constant float RAY_EPSILON = 0.0025f;
constant float INF = 1.0e30f;
constant int MAX_CLOSEST_STEPS = 8192;
constant int MAX_SHADOW_STEPS = 4096;
constant uint STATIONARY_PHASE_GRID = 2u;
constant uint RESET_PHASE_GRID = 1u;
constant uint MOVING_PHASE_GRID = 4u;

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

float3 safeInverse(float3 direction)
{
    return float3(
        fabs(direction.x) > 1.0e-8f ? 1.0f / direction.x : 1.0e30f,
        fabs(direction.y) > 1.0e-8f ? 1.0f / direction.y : 1.0e30f,
        fabs(direction.z) > 1.0e-8f ? 1.0f / direction.z : 1.0e30f
    );
}

float aabbEntry(float3 origin, float3 inverse_direction, float3 bmin, float3 bmax, float max_distance)
{
    float3 t0 = (bmin - origin) * inverse_direction;
    float3 t1 = (bmax - origin) * inverse_direction;
    float3 near_t = min(t0, t1);
    float3 far_t = max(t0, t1);
    float enter = max(max(near_t.x, near_t.y), max(near_t.z, 0.0f));
    float exit_distance = min(min(far_t.x, far_t.y), far_t.z);
    return exit_distance >= enter && enter < max_distance ? enter : INF;
}

bool hitTriangle(
    float3 origin,
    float3 direction,
    Triangle triangle,
    thread float& distance,
    thread float3& barycentric)
{
    float3 edge1 = triangle.p1.xyz - triangle.p0.xyz;
    float3 edge2 = triangle.p2.xyz - triangle.p0.xyz;
    float3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);
    if (fabs(determinant) < 1.0e-8f) return false;

    float inverse_determinant = 1.0f / determinant;
    float3 offset = origin - triangle.p0.xyz;
    float u = dot(offset, p) * inverse_determinant;
    if (u < 0.0f || u > 1.0f) return false;

    float3 q = cross(offset, edge1);
    float v = dot(direction, q) * inverse_determinant;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = dot(edge2, q) * inverse_determinant;
    if (t <= RAY_EPSILON || t >= distance) return false;

    distance = t;
    barycentric = float3(1.0f - u - v, u, v);
    return true;
}

Hit traceClosest(
    float3 origin,
    float3 direction,
    float max_distance,
    device const Node *nodes,
    device const Triangle *triangles,
    constant TraceUniforms& uniforms)
{
    Hit best;
    best.found = false;
    best.distance = max_distance;
    best.position = float3(0.0f);
    best.normal = float3(0.0f, 1.0f, 0.0f);
    best.geometric_normal = float3(0.0f, 1.0f, 0.0f);
    best.uv = float2(0.0f);
    best.material = 0u;

    const int node_count = max(uniforms.counts.x, 0);
    const int triangle_count = max(uniforms.counts.y, 0);
    if (node_count <= 0 || triangle_count <= 0) return best;

    float3 inverse_direction = safeInverse(direction);
    uint node_index = 0u;
    int steps = 0;

    while (node_index < uint(node_count) && steps++ < MAX_CLOSEST_STEPS) {
        Node node = nodes[node_index];
        if (aabbEntry(origin, inverse_direction, float3(node.bmin), float3(node.bmax), best.distance) >= INF) {
            node_index = node.extra.x;
            continue;
        }

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            uint end = min(node.first + count, uint(triangle_count));
            for (uint triangle_index = node.first; triangle_index < end; ++triangle_index) {
                Triangle triangle = triangles[triangle_index];
                float distance = best.distance;
                float3 barycentric;
                if (!hitTriangle(origin, direction, triangle, distance, barycentric)) continue;

                best.found = true;
                best.distance = distance;
                best.position = origin + direction * distance;
                best.normal = normalize(
                    triangle.n0.xyz * barycentric.x +
                    triangle.n1.xyz * barycentric.y +
                    triangle.n2.xyz * barycentric.z
                );
                best.geometric_normal = normalize(cross(
                    triangle.p1.xyz - triangle.p0.xyz,
                    triangle.p2.xyz - triangle.p0.xyz
                ));
                if (dot(best.geometric_normal, direction) > 0.0f) best.geometric_normal = -best.geometric_normal;
                if (dot(best.normal, best.geometric_normal) < 0.0f) best.normal = -best.normal;
                best.uv =
                    triangle.uv01.xy * barycentric.x +
                    triangle.uv01.zw * barycentric.y +
                    triangle.uv2.xy * barycentric.z;
                best.material = as_type<uint>(triangle.p0.w);
            }
            node_index = node.extra.x;
        } else {
            node_index = node.first;
        }
    }

    return best;
}

bool traceAny(
    float3 origin,
    float3 direction,
    float max_distance,
    device const Node *nodes,
    device const Triangle *triangles,
    constant TraceUniforms& uniforms)
{
    const int node_count = max(uniforms.counts.x, 0);
    const int triangle_count = max(uniforms.counts.y, 0);
    if (node_count <= 0 || triangle_count <= 0 || max_distance <= RAY_EPSILON) return false;

    float3 inverse_direction = safeInverse(direction);
    uint node_index = 0u;
    int steps = 0;

    while (node_index < uint(node_count) && steps++ < MAX_SHADOW_STEPS) {
        Node node = nodes[node_index];
        if (aabbEntry(origin, inverse_direction, float3(node.bmin), float3(node.bmax), max_distance) >= INF) {
            node_index = node.extra.x;
            continue;
        }

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            uint end = min(node.first + count, uint(triangle_count));
            for (uint triangle_index = node.first; triangle_index < end; ++triangle_index) {
                Triangle triangle = triangles[triangle_index];
                float distance = max_distance;
                float3 barycentric;
                if (hitTriangle(origin, direction, triangle, distance, barycentric)) return true;
            }
            node_index = node.extra.x;
        } else {
            node_index = node.first;
        }
    }

    return false;
}

float4 sampleTextureSlot(
    int slot,
    float2 uv,
    array<texture2d<float>, 16> textures,
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
        default: return float4(1.0f);
    }
}

float3 materialAlbedo(
    uint material_index,
    float2 uv,
    device const Material *materials,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 16> textures,
    sampler material_sampler)
{
    const int material_count = max(uniforms.counts.z, 0);
    if (material_index >= uint(material_count)) return float3(1.0f);
    Material material = materials[material_index];
    float3 albedo = max(material.base_color.rgb, float3(0.0f));
    int slot = material.data.x;
    if (slot >= 0 && slot < 16) {
        float4 texel = sampleTextureSlot(slot, uv, textures, material_sampler);
        albedo *= pow(max(texel.rgb, float3(0.0f)), float3(2.2f));
    }
    return clamp(albedo, float3(0.0f), float3(1.0f));
}

float3 cosineHemisphere(float3 normal, thread uint& state)
{
    float u1 = randomFloat(state);
    float u2 = randomFloat(state);
    float radius = sqrt(u1);
    float phi = 2.0f * PI * u2;
    float3 local = float3(radius * cos(phi), radius * sin(phi), sqrt(max(0.0f, 1.0f - u1)));
    float3 helper = fabs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(helper, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

float3 tracePath(
    float3 origin,
    float3 direction,
    thread uint& state,
    device const Node *nodes,
    device const Triangle *triangles,
    device const Material *materials,
    constant TraceUniforms& uniforms,
    array<texture2d<float>, 16> textures,
    sampler material_sampler)
{
    float3 radiance = float3(0.0f);
    float3 throughput = float3(1.0f);
    int bounce_count = clamp(uniforms.counts.w, 1, 4);

    for (int bounce = 0; bounce < bounce_count; ++bounce) {
        Hit hit = traceClosest(origin, direction, INF, nodes, triangles, uniforms);
        if (!hit.found) break;

        float3 albedo = materialAlbedo(hit.material, hit.uv, materials, uniforms, textures, material_sampler);

        if (uniforms.frame.z != 0u && uniforms.light_position_intensity.w > 0.0f) {
            float3 to_light = uniforms.light_position_intensity.xyz - hit.position;
            float distance_squared = max(dot(to_light, to_light), 1.0e-4f);
            float light_distance = sqrt(distance_squared);
            float3 light_direction = to_light / light_distance;
            float cosine = max(dot(hit.normal, light_direction), 0.0f);

            if (cosine > 0.0f) {
                float3 shadow_origin = hit.position + hit.geometric_normal * RAY_EPSILON * 4.0f;
                float shadow_distance = max(light_distance - RAY_EPSILON * 8.0f, 0.0f);
                if (!traceAny(shadow_origin, light_direction, shadow_distance, nodes, triangles, uniforms)) {
                    float3 incoming = max(uniforms.light_color_tan_half_fov.xyz, float3(0.0f)) *
                        max(uniforms.light_position_intensity.w, 0.0f) / distance_squared;
                    radiance += throughput * albedo * incoming * (cosine / PI);
                }
            }
        }

        throughput *= albedo;
        if (max(max(throughput.r, throughput.g), throughput.b) < 1.0e-4f) break;

        origin = hit.position + hit.geometric_normal * RAY_EPSILON * 4.0f;
        direction = cosineHemisphere(hit.normal, state);
        if (dot(direction, hit.geometric_normal) <= 0.0f) {
            direction = cosineHemisphere(hit.geometric_normal, state);
        }
    }

    return radiance;
}

kernel void trace_kernel(
    device const Node *nodes [[buffer(0)]],
    device const Triangle *triangles [[buffer(1)]],
    device const Material *materials [[buffer(2)]],
    constant TraceUniforms& uniforms [[buffer(3)]],
    texture2d<float, access::read_write> accumulation [[texture(0)]],
    array<texture2d<float>, 16> textures [[texture(1)]],
    sampler material_sampler [[sampler(0)]],
    uint2 pixel [[thread_position_in_grid]])
{
    uint2 size = uint2(uint(max(uniforms.resolution_aspect.x, 1.0f)), uint(max(uniforms.resolution_aspect.y, 1.0f)));
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    const bool reset = uniforms.frame.y != 0u;
    const bool camera_moving = uniforms.frame.w != 0u;
    if (reset) accumulation.write(float4(0.0f), pixel);

    uint phase_grid = camera_moving ? MOVING_PHASE_GRID :
        (reset ? RESET_PHASE_GRID : STATIONARY_PHASE_GRID);
    uint phase_count = phase_grid * phase_grid;
    uint phase = uniforms.frame.x % phase_count;
    uint phase_x = phase % phase_grid;
    uint phase_y = phase / phase_grid;
    uint pixel_phase = (pixel.x % phase_grid) + (pixel.y % phase_grid) * phase_grid;
    if (pixel_phase != phase) return;

    uint state = hashUint(
        pixel.x * 1973u ^
        pixel.y * 9277u ^
        uniforms.frame.x * 26699u ^
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

    float3 sample_radiance = tracePath(
        uniforms.camera_position.xyz,
        direction,
        state,
        nodes,
        triangles,
        materials,
        uniforms,
        textures,
        material_sampler
    );
    float4 previous = reset ? float4(0.0f) : accumulation.read(pixel);
    accumulation.write(float4(previous.rgb + sample_radiance, previous.a + 1.0f), pixel);
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

float4 validAccumulationSample(texture2d<float> accumulation, int2 pixel, int2 size)
{
    pixel = clamp(pixel, int2(0), size - int2(1));
    float4 exact = accumulation.read(uint2(pixel));
    if (exact.a > 0.0f) return exact;

    int2 block = (pixel / 4) * 4;
    float4 best = float4(0.0f);
    int best_distance = 1000;

    for (int phase = 0; phase < 16; ++phase) {
        int2 candidate = block + int2(phase & 3, (phase >> 2) & 3);
        candidate = clamp(candidate, int2(0), size - int2(1));
        float4 candidate_sample = accumulation.read(uint2(candidate));
        if (candidate_sample.a <= 0.0f) continue;

        int distance = abs(candidate.x - pixel.x) + abs(candidate.y - pixel.y);
        if (distance < best_distance) {
            best = candidate_sample;
            best_distance = distance;
        }
    }

    return best;
}

bool blockComplete(texture2d<float> accumulation, int2 pixel, int2 size)
{
    int2 block = (pixel / 4) * 4;
    for (int phase = 0; phase < 16; ++phase) {
        int2 candidate = block + int2(phase & 3, (phase >> 2) & 3);
        candidate = clamp(candidate, int2(0), size - int2(1));
        if (accumulation.read(uint2(candidate)).a <= 0.0f) return false;
    }
    return true;
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

    float4 accumulated = accumulation.read(uint2(pixel));
    if (uniforms.exposure.y > 0.5f && accumulated.a <= 0.0f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (uniforms.exposure.y <= 0.5f) {
        accumulated = blockComplete(accumulation, pixel, size)
            ? accumulation.sample(accumulation_sampler, sample_uv)
            : validAccumulationSample(accumulation, pixel, size);
    }

    float samples = max(accumulated.a, 1.0f);
    float3 linear_color = max(accumulated.rgb / samples, float3(0.0f));
    linear_color *= max(uniforms.exposure.x, 0.0f);
    float3 mapped = linear_color / (float3(1.0f) + linear_color);
    mapped = pow(mapped, float3(1.0f / 2.2f));
    return float4(mapped, 1.0f);
}
)MSL";

} // namespace Renderer::PathTracerMetalShaders

#endif
