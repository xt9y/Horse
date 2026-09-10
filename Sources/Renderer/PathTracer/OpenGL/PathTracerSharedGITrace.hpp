#ifndef RW_ENGINE_RENDERER_PATHTRACER_SHARED_GI_TRACE_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_SHARED_GI_TRACE_HPP

namespace Renderer::PathTracerShaders {

inline constexpr const char *trace = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

struct Node {
    vec3 bmin; uint first;
    vec3 bmax; uint meta;
    uvec4 extra;
};

struct Triangle {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 n0;
    vec4 n1;
    vec4 n2;
    vec4 uv01;
    vec4 uv2;
};

struct Material {
    vec4 base_color;
    ivec4 data;
};

layout(std430, binding = 0) readonly buffer Nodes { Node nodes[]; };
layout(std430, binding = 1) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 4) readonly buffer Materials { Material materials[]; };
layout(std430, binding = 5) readonly buffer SharedGiField { vec4 gi_data[]; };
layout(std430, binding = 6) readonly buffer EntityVisibility { uint entity_visibility[]; };

layout(rgba32f, binding = 0) uniform image2D uAccumulation;
layout(rgba32f, binding = 1) uniform image2D uPrimaryDepth;

uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform sampler2D uTexture2;
uniform sampler2D uTexture3;
uniform sampler2D uTexture4;
uniform sampler2D uTexture5;
uniform sampler2D uTexture6;
uniform sampler2D uTexture7;
uniform sampler2D uTexture8;
uniform sampler2D uTexture9;
uniform sampler2D uTexture10;
uniform sampler2D uTexture11;
uniform sampler2D uTexture12;
uniform sampler2D uTexture13;
uniform sampler2D uTexture14;
uniform sampler2D uTexture15;

uniform vec2 uResolution;
uniform vec3 uCameraPosition;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uTanHalfFov;
uniform float uAspect;

uniform int uNodeCount;
uniform int uTriangleCount;
uniform int uMaterialCount;
uniform int uSamplesThisFrame;
uniform int uSampleBase;
uniform int uFrameIndex;
uniform int uResetAccumulation;
uniform int uCameraMoving;
uniform int uVisibilityAll;
uniform int uStationaryPhaseGrid;
uniform int uResetPhaseGrid;
uniform int uMovingPhaseGrid;
uniform int uMovingDepthBlock;

uniform int uHasLight;
uniform vec3 uLightPosition;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform float uAlphaCutoff;

const uint LEAF_BIT = 0x80000000u;
const float PI = 3.14159265358979323846;
const float RAY_EPSILON = 0.0025;
const float INF = 1.0e30;
const float SH_Y00 = 0.2820947918;
const float SH_Y1 = 0.4886025119;
const int MAX_CLOSEST_STEPS = 8192;
const int MAX_SHADOW_STEPS = 4096;
const int GI_HEADER_VEC4S = 4;

struct Hit {
    bool found;
    float distance;
    vec3 position;
    vec3 normal;
    vec3 geometric_normal;
    vec2 uv;
    uint material;
};

uint hashUint(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float randomFloat(inout uint state)
{
    state = hashUint(state);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 safeInverse(vec3 direction)
{
    return vec3(
        abs(direction.x) > 1.0e-8 ? 1.0 / direction.x : 1.0e30,
        abs(direction.y) > 1.0e-8 ? 1.0 / direction.y : 1.0e30,
        abs(direction.z) > 1.0e-8 ? 1.0 / direction.z : 1.0e30
    );
}

float aabbEntry(vec3 origin, vec3 inverse_direction, vec3 bmin, vec3 bmax, float max_distance)
{
    vec3 t0 = (bmin - origin) * inverse_direction;
    vec3 t1 = (bmax - origin) * inverse_direction;
    vec3 near_t = min(t0, t1);
    vec3 far_t = max(t0, t1);
    float enter = max(max(near_t.x, near_t.y), max(near_t.z, 0.0));
    float exit = min(min(far_t.x, far_t.y), far_t.z);
    return exit >= enter && enter < max_distance ? enter : INF;
}

bool hitTriangle(
    vec3 origin,
    vec3 direction,
    Triangle triangle,
    inout float distance,
    out vec3 barycentric)
{
    vec3 edge1 = triangle.p1.xyz - triangle.p0.xyz;
    vec3 edge2 = triangle.p2.xyz - triangle.p0.xyz;
    vec3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);
    if (abs(determinant) < 1.0e-8) return false;

    float inverse_determinant = 1.0 / determinant;
    vec3 offset = origin - triangle.p0.xyz;
    float u = dot(offset, p) * inverse_determinant;
    if (u < 0.0 || u > 1.0) return false;

    vec3 q = cross(offset, edge1);
    float v = dot(direction, q) * inverse_determinant;
    if (v < 0.0 || u + v > 1.0) return false;

    float t = dot(edge2, q) * inverse_determinant;
    if (t <= RAY_EPSILON || t >= distance) return false;

    distance = t;
    barycentric = vec3(1.0 - u - v, u, v);
    return true;
}

vec4 sampleTextureSlot(int slot, vec2 uv);

bool alphaCutoutPass(uint material_index, vec2 uv)
{
    if (material_index >= uint(max(uMaterialCount, 0))) return true;
    Material material = materials[material_index];
    float alpha = clamp(material.base_color.a, 0.0, 1.0);
    int slot = material.data.x;
    if (slot >= 0 && slot < 16) {
        vec4 texel = sampleTextureSlot(slot, uv);
        alpha *= texel.a;
    }
    return alpha >= uAlphaCutoff;
}

bool triangleVisible(Triangle triangle)
{
    return uVisibilityAll != 0 || entity_visibility[floatBitsToUint(triangle.p1.w)] != 0u;
}

Hit traceClosest(vec3 origin, vec3 direction, float max_distance)
{
    Hit best;
    best.found = false;
    best.distance = max_distance;
    best.position = vec3(0.0);
    best.normal = vec3(0.0, 1.0, 0.0);
    best.geometric_normal = vec3(0.0, 1.0, 0.0);
    best.uv = vec2(0.0);
    best.material = 0u;

    if (uNodeCount <= 0 || uTriangleCount <= 0) return best;

    vec3 inverse_direction = safeInverse(direction);
    uint node_index = 0u;
    int steps = 0;

    while (node_index < uint(uNodeCount) && steps++ < MAX_CLOSEST_STEPS) {
        Node node = nodes[node_index];
        if (aabbEntry(origin, inverse_direction, node.bmin, node.bmax, best.distance) >= INF) {
            node_index = node.extra.x;
            continue;
        }

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            uint end = min(node.first + count, uint(uTriangleCount));
            for (uint triangle_index = node.first; triangle_index < end; ++triangle_index) {
                Triangle triangle = triangles[triangle_index];
                if (!triangleVisible(triangle)) continue;
                float distance = best.distance;
                vec3 barycentric;
                if (!hitTriangle(origin, direction, triangle, distance, barycentric)) continue;

                uint material_index = floatBitsToUint(triangle.p0.w);
                vec2 candidate_uv =
                    triangle.uv01.xy * barycentric.x +
                    triangle.uv01.zw * barycentric.y +
                    triangle.uv2.xy * barycentric.z;
                if (!alphaCutoutPass(material_index, candidate_uv)) continue;

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
                if (dot(best.geometric_normal, direction) > 0.0) best.geometric_normal = -best.geometric_normal;
                if (dot(best.normal, best.geometric_normal) < 0.0) best.normal = -best.normal;
                best.uv = candidate_uv;
                best.material = material_index;
            }
            node_index = node.extra.x;
        } else {
            node_index = node.first;
        }
    }

    return best;
}

float deterministicDepth(vec2 sample_pixel)
{
    vec2 depth_uv = sample_pixel / uResolution;
    vec2 depth_ndc = depth_uv * 2.0 - 1.0;
    vec3 depth_direction = normalize(
        uCameraForward +
        uCameraRight * (depth_ndc.x * uAspect * uTanHalfFov) +
        uCameraUp * (depth_ndc.y * uTanHalfFov)
    );
    Hit depth_hit = traceClosest(uCameraPosition, depth_direction, INF);
    if (!depth_hit.found) return INF;
    return max(
        dot(depth_hit.position - uCameraPosition, normalize(uCameraForward)),
        RAY_EPSILON
    );
}

bool traceAny(vec3 origin, vec3 direction, float max_distance)
{
    if (uNodeCount <= 0 || uTriangleCount <= 0 || max_distance <= RAY_EPSILON) return false;

    vec3 inverse_direction = safeInverse(direction);
    uint node_index = 0u;
    int steps = 0;

    while (node_index < uint(uNodeCount) && steps++ < MAX_SHADOW_STEPS) {
        Node node = nodes[node_index];
        if (aabbEntry(origin, inverse_direction, node.bmin, node.bmax, max_distance) >= INF) {
            node_index = node.extra.x;
            continue;
        }

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            uint end = min(node.first + count, uint(uTriangleCount));
            for (uint triangle_index = node.first; triangle_index < end; ++triangle_index) {
                Triangle triangle = triangles[triangle_index];
                if (!triangleVisible(triangle)) continue;
                float distance = max_distance;
                vec3 barycentric;
                if (!hitTriangle(origin, direction, triangle, distance, barycentric)) continue;
                uint material_index = floatBitsToUint(triangle.p0.w);
                vec2 candidate_uv =
                    triangle.uv01.xy * barycentric.x +
                    triangle.uv01.zw * barycentric.y +
                    triangle.uv2.xy * barycentric.z;
                if (!alphaCutoutPass(material_index, candidate_uv)) continue;
                return true;
            }
            node_index = node.extra.x;
        } else {
            node_index = node.first;
        }
    }

    return false;
}

vec4 sampleTextureSlot(int slot, vec2 uv)
{
    vec2 coordinates = vec2(uv.x, 1.0 - uv.y);
    switch (slot) {
        case 0: return texture(uTexture0, coordinates);
        case 1: return texture(uTexture1, coordinates);
        case 2: return texture(uTexture2, coordinates);
        case 3: return texture(uTexture3, coordinates);
        case 4: return texture(uTexture4, coordinates);
        case 5: return texture(uTexture5, coordinates);
        case 6: return texture(uTexture6, coordinates);
        case 7: return texture(uTexture7, coordinates);
        case 8: return texture(uTexture8, coordinates);
        case 9: return texture(uTexture9, coordinates);
        case 10: return texture(uTexture10, coordinates);
        case 11: return texture(uTexture11, coordinates);
        case 12: return texture(uTexture12, coordinates);
        case 13: return texture(uTexture13, coordinates);
        case 14: return texture(uTexture14, coordinates);
        case 15: return texture(uTexture15, coordinates);
        default: return vec4(1.0);
    }
}

vec3 materialAlbedo(uint material_index, vec2 uv)
{
    if (material_index >= uint(max(uMaterialCount, 0))) return vec3(1.0);
    Material material = materials[material_index];
    vec3 albedo = max(material.base_color.rgb, vec3(0.0));
    int slot = material.data.x;
    if (slot >= 0 && slot < 16) {
        vec4 texel = sampleTextureSlot(slot, uv);
        albedo *= pow(max(texel.rgb, vec3(0.0)), vec3(2.2));
    }
    return clamp(albedo, vec3(0.0), vec3(1.0));
}

int giProbeIndex(ivec3 coordinate, ivec3 dimensions)
{
    return coordinate.x + dimensions.x * (coordinate.y + dimensions.y * coordinate.z);
}

vec3 giCoefficient(ivec3 coordinate, ivec3 dimensions, int coefficient)
{
    int probe = giProbeIndex(coordinate, dimensions);
    return gi_data[GI_HEADER_VEC4S + probe * 4 + coefficient].xyz;
}

vec3 sampleGlobalIllumination(vec3 position, vec3 normal)
{
    if (gi_data[0].w < 0.5) return vec3(0.0);

    vec3 minimum = gi_data[0].xyz;
    vec3 maximum = gi_data[1].xyz;
    float intensity = max(gi_data[1].w, 0.0);
    ivec3 dimensions = max(ivec3(gi_data[2].xyz + vec3(0.5)), ivec3(2));
    vec3 extent = max(maximum - minimum, vec3(1.0e-6));
    vec3 grid = clamp((position - minimum) / extent, vec3(0.0), vec3(1.0)) * vec3(dimensions - 1);
    ivec3 i0 = ivec3(floor(grid));
    ivec3 i1 = min(i0 + ivec3(1), dimensions - ivec3(1));
    vec3 t = grid - vec3(i0);

    vec3 coefficients[4];
    for (int coefficient = 0; coefficient < 4; ++coefficient) {
        vec3 c000 = giCoefficient(ivec3(i0.x, i0.y, i0.z), dimensions, coefficient);
        vec3 c100 = giCoefficient(ivec3(i1.x, i0.y, i0.z), dimensions, coefficient);
        vec3 c010 = giCoefficient(ivec3(i0.x, i1.y, i0.z), dimensions, coefficient);
        vec3 c110 = giCoefficient(ivec3(i1.x, i1.y, i0.z), dimensions, coefficient);
        vec3 c001 = giCoefficient(ivec3(i0.x, i0.y, i1.z), dimensions, coefficient);
        vec3 c101 = giCoefficient(ivec3(i1.x, i0.y, i1.z), dimensions, coefficient);
        vec3 c011 = giCoefficient(ivec3(i0.x, i1.y, i1.z), dimensions, coefficient);
        vec3 c111 = giCoefficient(ivec3(i1.x, i1.y, i1.z), dimensions, coefficient);
        vec3 c00 = mix(c000, c100, t.x);
        vec3 c10 = mix(c010, c110, t.x);
        vec3 c01 = mix(c001, c101, t.x);
        vec3 c11 = mix(c011, c111, t.x);
        coefficients[coefficient] = mix(mix(c00, c10, t.y), mix(c01, c11, t.y), t.z);
    }

    vec3 n = normalize(normal);
    vec3 irradiance = coefficients[0] * (PI * SH_Y00);
    irradiance += coefficients[1] * ((2.0 * PI / 3.0) * SH_Y1 * n.x);
    irradiance += coefficients[2] * ((2.0 * PI / 3.0) * SH_Y1 * n.y);
    irradiance += coefficients[3] * ((2.0 * PI / 3.0) * SH_Y1 * n.z);
    return max(irradiance, vec3(0.0)) * intensity;
}

vec3 tracePath(vec3 origin, vec3 direction)
{
    Hit hit = traceClosest(origin, direction, INF);
    if (!hit.found) return vec3(0.0);

    vec3 albedo = materialAlbedo(hit.material, hit.uv);
    vec3 radiance = vec3(0.0);

    if (uHasLight != 0 && uLightIntensity > 0.0) {
        vec3 to_light = uLightPosition - hit.position;
        float distance_squared = max(dot(to_light, to_light), 1.0e-4);
        float light_distance = sqrt(distance_squared);
        vec3 light_direction = to_light / light_distance;
        float cosine = max(dot(hit.normal, light_direction), 0.0);

        if (cosine > 0.0) {
            vec3 shadow_origin = hit.position + hit.geometric_normal * RAY_EPSILON * 4.0;
            float shadow_distance = max(light_distance - RAY_EPSILON * 8.0, 0.0);
            if (!traceAny(shadow_origin, light_direction, shadow_distance)) {
                vec3 incoming = max(uLightColor, vec3(0.0)) * max(uLightIntensity, 0.0) / distance_squared;
                radiance += albedo * incoming * (cosine / PI);
            }
        }
    }

    vec3 indirect_irradiance = sampleGlobalIllumination(hit.position, hit.normal);
    radiance += albedo * indirect_irradiance * (1.0 / PI);
    return radiance;
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(uResolution);
    if (any(greaterThanEqual(pixel, size))) return;

    if (uResetAccumulation != 0) {
        imageStore(uAccumulation, pixel, vec4(0.0));
    }

    if (uCameraMoving != 0) {
        int moving_depth_block = max(uMovingDepthBlock, 1);
        if ((pixel.x % moving_depth_block) == 0 && (pixel.y % moving_depth_block) == 0) {
            vec2 sample_pixel = min(
                vec2(pixel) + vec2(float(moving_depth_block) * 0.5),
                uResolution - vec2(0.5)
            );
            float depth_value = deterministicDepth(sample_pixel);
            ivec2 block_end = min(pixel + ivec2(moving_depth_block), size);
            for (int y = pixel.y; y < block_end.y; ++y) {
                for (int x = pixel.x; x < block_end.x; ++x) {
                    ivec2 target = ivec2(x, y);
                    imageStore(uPrimaryDepth, target, vec4(depth_value, 0.0, 0.0, 1.0));
                }
            }
        }
    } else if (uResetAccumulation != 0) {
        float depth_value = deterministicDepth(vec2(pixel) + vec2(0.5));
        imageStore(uPrimaryDepth, pixel, vec4(depth_value, 0.0, 0.0, 1.0));
    }

    int phase_grid = uCameraMoving != 0 ? max(uMovingPhaseGrid, 1) :
        (uResetAccumulation != 0 ? max(uResetPhaseGrid, 1) : max(uStationaryPhaseGrid, 1));
    int phase_count = phase_grid * phase_grid;
    int phase = max(uFrameIndex, 0) % phase_count;
    int phase_x = phase % phase_grid;
    int phase_y = phase / phase_grid;
    int pixel_phase = (pixel.x % phase_grid) + (pixel.y % phase_grid) * phase_grid;
    if (pixel_phase != phase) return;

    int samples = max(uSamplesThisFrame, 1);
    vec3 sample_radiance = vec3(0.0);
    for (int sample = 0; sample < samples; ++sample) {
        uint absolute_sample = uint(max(uSampleBase + sample, 0));
        uint state = hashUint(
            uint(pixel.x) * 1973u ^
            uint(pixel.y) * 9277u ^
            absolute_sample * 26699u ^
            uint(phase_x + phase_y * phase_grid) * 104729u ^
            0x68bc21ebu
        );

        vec2 jitter = vec2(randomFloat(state), randomFloat(state)) - 0.5;
        vec2 uv = (vec2(pixel) + vec2(0.5) + jitter) / uResolution;
        vec2 ndc = uv * 2.0 - 1.0;
        vec3 direction = normalize(
            uCameraForward +
            uCameraRight * (ndc.x * uAspect * uTanHalfFov) +
            uCameraUp * (ndc.y * uTanHalfFov)
        );

        sample_radiance += tracePath(uCameraPosition, direction);
    }
    vec4 previous = uResetAccumulation != 0 ? vec4(0.0) : imageLoad(uAccumulation, pixel);
    imageStore(uAccumulation, pixel, vec4(previous.rgb + sample_radiance, previous.a + float(samples)));
}
)GLSL";

} // namespace Renderer::PathTracerShaders

#endif
