#ifndef RW_ENGINE_RENDERER_PATHTRACER_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_SHADERS_HPP

namespace Renderer::PathTracerShaders {

inline constexpr const char *trace = R"GLSL(
#version 430
layout(local_size_x = 8, local_size_y = 8) in;

struct Node {
    vec3 bmin; uint left;
    vec3 bmax; uint meta;
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

struct Instance {
    mat4 object_to_world;
    mat4 world_to_object;
    uvec4 data;
};

struct Material {
    vec4 base_color;
    ivec4 data;
};

layout(std430, binding = 0) readonly buffer BlasNodes { Node blas_nodes[]; };
layout(std430, binding = 1) readonly buffer Triangles { Triangle triangles[]; };
layout(std430, binding = 2) readonly buffer TlasNodes { Node tlas_nodes[]; };
layout(std430, binding = 3) readonly buffer Instances { Instance instances[]; };
layout(std430, binding = 4) readonly buffer Materials { Material materials[]; };

layout(rgba32f, binding = 0) uniform image2D uAccumulation;

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

uniform int uTlasNodeCount;
uniform int uInstanceCount;
uniform int uMaterialCount;
uniform int uSamplesThisFrame;
uniform int uSampleBase;
uniform int uMaxBounces;

uniform int uHasLight;
uniform vec3 uLightPosition;
uniform vec3 uLightColor;
uniform float uLightIntensity;

const uint LEAF_BIT = 0x80000000u;
const float PI = 3.14159265358979323846;
const float RAY_EPSILON = 0.0025;

struct Hit {
    bool found;
    float distance;
    vec3 position;
    vec3 normal;
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

bool hitAabb(vec3 origin, vec3 inverse_direction, vec3 bmin, vec3 bmax, float max_distance)
{
    vec3 t0 = (bmin - origin) * inverse_direction;
    vec3 t1 = (bmax - origin) * inverse_direction;
    vec3 near_t = min(t0, t1);
    vec3 far_t = max(t0, t1);
    float enter = max(max(near_t.x, near_t.y), max(near_t.z, 0.0));
    float exit = min(min(far_t.x, far_t.y), far_t.z);
    return exit >= enter && enter < max_distance;
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

vec3 transformPoint(mat4 matrix, vec3 point)
{
    vec4 result = matrix * vec4(point, 1.0);
    return result.xyz / max(abs(result.w), 1.0e-8);
}

vec3 transformDirection(mat4 matrix, vec3 direction)
{
    return (matrix * vec4(direction, 0.0)).xyz;
}

bool traceInstance(
    uint instance_index,
    vec3 world_origin,
    vec3 world_direction,
    float max_world_distance,
    inout Hit best)
{
    if (instance_index >= uint(uInstanceCount)) return false;
    Instance instance = instances[instance_index];
    if (instance.data.z == 0u) return false;

    vec3 local_origin = transformPoint(instance.world_to_object, world_origin);
    vec3 local_direction_raw = transformDirection(instance.world_to_object, world_direction);
    float local_length = length(local_direction_raw);
    if (local_length <= 1.0e-10) return false;
    vec3 local_direction = local_direction_raw / local_length;
    vec3 inverse_direction = safeInverse(local_direction);

    uint stack[48];
    int stack_size = 0;
    int steps = 0;
    stack[stack_size++] = 0u;
    bool found = false;

    while (stack_size > 0 && steps++ < 2048) {
        uint local_node_index = stack[--stack_size];
        if (local_node_index >= instance.data.z) continue;
        Node node = blas_nodes[instance.data.x + local_node_index];
        if (!hitAabb(local_origin, inverse_direction, node.bmin, node.bmax, 1.0e30)) continue;

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            for (uint index = 0u; index < count; ++index) {
                Triangle triangle = triangles[instance.data.y + node.left + index];
                float local_distance = 1.0e30;
                vec3 barycentric;
                if (!hitTriangle(local_origin, local_direction, triangle, local_distance, barycentric)) continue;

                vec3 local_position = local_origin + local_direction * local_distance;
                vec3 world_position = transformPoint(instance.object_to_world, local_position);
                float world_distance = length(world_position - world_origin);
                if (world_distance >= best.distance || world_distance >= max_world_distance) continue;

                vec3 local_normal = normalize(
                    triangle.n0.xyz * barycentric.x +
                    triangle.n1.xyz * barycentric.y +
                    triangle.n2.xyz * barycentric.z
                );
                vec3 world_normal = normalize(transpose(mat3(instance.world_to_object)) * local_normal);
                if (dot(world_normal, world_direction) > 0.0) world_normal = -world_normal;

                vec2 uv0 = triangle.uv01.xy;
                vec2 uv1 = triangle.uv01.zw;
                vec2 uv2 = triangle.uv2.xy;

                best.found = true;
                best.distance = world_distance;
                best.position = world_position;
                best.normal = world_normal;
                best.uv = uv0 * barycentric.x + uv1 * barycentric.y + uv2 * barycentric.z;
                best.material = instance.data.w;
                found = true;
            }
        } else if (stack_size <= 45) {
            stack[stack_size++] = node.meta;
            stack[stack_size++] = node.left;
        }
    }

    return found;
}

Hit traceScene(vec3 origin, vec3 direction, float max_distance)
{
    Hit best;
    best.found = false;
    best.distance = max_distance;
    best.position = vec3(0.0);
    best.normal = vec3(0.0, 1.0, 0.0);
    best.uv = vec2(0.0);
    best.material = 0u;

    if (uTlasNodeCount <= 0 || uInstanceCount <= 0) return best;

    vec3 inverse_direction = safeInverse(direction);
    uint stack[48];
    int stack_size = 0;
    int steps = 0;
    stack[stack_size++] = 0u;

    while (stack_size > 0 && steps++ < 2048) {
        uint node_index = stack[--stack_size];
        if (node_index >= uint(uTlasNodeCount)) continue;
        Node node = tlas_nodes[node_index];
        if (!hitAabb(origin, inverse_direction, node.bmin, node.bmax, best.distance)) continue;

        if ((node.meta & LEAF_BIT) != 0u) {
            uint count = node.meta & ~LEAF_BIT;
            for (uint index = 0u; index < count; ++index) {
                traceInstance(node.left + index, origin, direction, best.distance, best);
            }
        } else if (stack_size <= 45) {
            stack[stack_size++] = node.meta;
            stack[stack_size++] = node.left;
        }
    }

    return best;
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

vec3 cosineHemisphere(vec3 normal, inout uint state)
{
    float u1 = randomFloat(state);
    float u2 = randomFloat(state);
    float radius = sqrt(u1);
    float phi = 2.0 * PI * u2;
    vec3 local = vec3(radius * cos(phi), radius * sin(phi), sqrt(max(0.0, 1.0 - u1)));

    vec3 helper = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

bool visibleToPointLight(vec3 position, vec3 normal, vec3 light_direction, float light_distance)
{
    vec3 origin = position + normal * RAY_EPSILON * 4.0;
    Hit blocker = traceScene(origin, light_direction, max(light_distance - RAY_EPSILON * 8.0, 0.0));
    return !blocker.found;
}

vec3 tracePath(vec3 origin, vec3 direction, inout uint state)
{
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    int bounce_count = clamp(uMaxBounces, 1, 8);

    for (int bounce = 0; bounce < bounce_count; ++bounce) {
        Hit hit = traceScene(origin, direction, 1.0e30);
        if (!hit.found) break;

        vec3 albedo = materialAlbedo(hit.material, hit.uv);

        if (uHasLight != 0 && uLightIntensity > 0.0) {
            vec3 to_light = uLightPosition - hit.position;
            float distance_squared = max(dot(to_light, to_light), 1.0e-4);
            float light_distance = sqrt(distance_squared);
            vec3 light_direction = to_light / light_distance;
            float cosine = max(dot(hit.normal, light_direction), 0.0);

            if (cosine > 0.0 && visibleToPointLight(hit.position, hit.normal, light_direction, light_distance)) {
                vec3 incoming = max(uLightColor, vec3(0.0)) * max(uLightIntensity, 0.0) / distance_squared;
                radiance += throughput * albedo * incoming * (cosine / PI);
            }
        }

        throughput *= albedo;
        if (max(max(throughput.r, throughput.g), throughput.b) < 1.0e-4) break;

        if (bounce >= 2) {
            float survival = clamp(max(max(throughput.r, throughput.g), throughput.b), 0.1, 0.95);
            if (randomFloat(state) > survival) break;
            throughput /= survival;
        }

        origin = hit.position + hit.normal * RAY_EPSILON * 4.0;
        direction = cosineHemisphere(hit.normal, state);
    }

    return radiance;
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(uResolution);
    if (any(greaterThanEqual(pixel, size))) return;

    int samples = clamp(uSamplesThisFrame, 1, 4);
    vec3 frame_sum = vec3(0.0);

    for (int sample_index = 0; sample_index < samples; ++sample_index) {
        uint absolute_sample = uint(max(uSampleBase + sample_index, 0));
        uint state = hashUint(
            uint(pixel.x) * 1973u ^
            uint(pixel.y) * 9277u ^
            absolute_sample * 26699u ^
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

        frame_sum += tracePath(uCameraPosition, direction, state);
    }

    vec4 previous = uSampleBase == 0 ? vec4(0.0) : imageLoad(uAccumulation, pixel);
    imageStore(uAccumulation, pixel, vec4(previous.rgb + frame_sum, 1.0));
}
)GLSL";

inline constexpr const char *present_vertex = R"GLSL(
#version 430 compatibility
out vec2 vUv;
void main()
{
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
    vUv = gl_Vertex.xy * 0.5 + 0.5;
}
)GLSL";

inline constexpr const char *present_fragment = R"GLSL(
#version 430 compatibility
uniform sampler2D uAccumulation;
uniform float uSampleCount;
uniform float uExposure;
in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main()
{
    float samples = max(uSampleCount, 1.0);
    vec3 linear_color = max(texture(uAccumulation, vUv).rgb / samples, vec3(0.0));
    linear_color *= max(uExposure, 0.0);
    vec3 mapped = linear_color / (vec3(1.0) + linear_color);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    outColor = vec4(mapped, 1.0);
}
)GLSL";

} // namespace Renderer::PathTracerShaders

#endif
