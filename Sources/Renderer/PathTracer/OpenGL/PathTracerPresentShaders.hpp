#ifndef RW_ENGINE_RENDERER_PATHTRACER_PRESENT_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_PRESENT_SHADERS_HPP

namespace Renderer::PathTracerShaders {

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
uniform float uPhaseCount;
uniform int uCameraMoving;
uniform float uExposure;
in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec4 reconstructSparseSample(ivec2 pixel, ivec2 size)
{
    pixel = clamp(pixel, ivec2(0), size - ivec2(1));
    ivec2 block = (pixel / 4) * 4;
    ivec2 phase_offset = ivec2(-1);
    vec4 fallback = vec4(0.0);

    for (int phase = 0; phase < 16; ++phase) {
        ivec2 candidate = block + ivec2(phase & 3, (phase >> 2) & 3);
        if (any(greaterThanEqual(candidate, size))) continue;
        vec4 sample_value = texelFetch(uAccumulation, candidate, 0);
        if (sample_value.a <= 0.0) continue;
        phase_offset = candidate - block;
        fallback = sample_value;
        break;
    }

    if (phase_offset.x < 0) {
        // Edge blocks can miss the active phase. Find the closest available
        // lattice sample without ever introducing a synthetic black texel.
        int best_distance = 1000000;
        for (int y = -4; y <= 4; ++y) {
            for (int x = -4; x <= 4; ++x) {
                ivec2 candidate = clamp(pixel + ivec2(x, y), ivec2(0), size - ivec2(1));
                vec4 sample_value = texelFetch(uAccumulation, candidate, 0);
                if (sample_value.a <= 0.0) continue;
                int distance = x * x + y * y;
                if (distance < best_distance) {
                    fallback = sample_value;
                    phase_offset = ivec2(candidate.x & 3, candidate.y & 3);
                    best_distance = distance;
                }
            }
        }
        if (phase_offset.x < 0) return vec4(0.0);
    }

    vec2 lattice = (vec2(pixel) - vec2(phase_offset)) * 0.25;
    ivec2 max_cell = max((size - ivec2(1) - phase_offset) / 4, ivec2(0));
    vec2 grid = clamp(lattice, vec2(0.0), vec2(max_cell));
    ivec2 cell0 = ivec2(floor(grid));
    ivec2 cell1 = min(cell0 + ivec2(1), max_cell);
    vec2 t = grid - vec2(cell0);

    ivec2 p00 = cell0 * 4 + phase_offset;
    ivec2 p10 = ivec2(cell1.x, cell0.y) * 4 + phase_offset;
    ivec2 p01 = ivec2(cell0.x, cell1.y) * 4 + phase_offset;
    ivec2 p11 = cell1 * 4 + phase_offset;

    vec4 s00 = texelFetch(uAccumulation, p00, 0);
    vec4 s10 = texelFetch(uAccumulation, p10, 0);
    vec4 s01 = texelFetch(uAccumulation, p01, 0);
    vec4 s11 = texelFetch(uAccumulation, p11, 0);

    float w00 = (1.0 - t.x) * (1.0 - t.y) * (s00.a > 0.0 ? 1.0 : 0.0);
    float w10 = t.x * (1.0 - t.y) * (s10.a > 0.0 ? 1.0 : 0.0);
    float w01 = (1.0 - t.x) * t.y * (s01.a > 0.0 ? 1.0 : 0.0);
    float w11 = t.x * t.y * (s11.a > 0.0 ? 1.0 : 0.0);
    float weight_sum = w00 + w10 + w01 + w11;
    if (weight_sum <= 1.0e-6) return fallback;
    return (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) / weight_sum;
}

bool blockComplete(ivec2 pixel, ivec2 size)
{
    ivec2 block = (pixel / 4) * 4;
    for (int phase = 0; phase < 16; ++phase) {
        ivec2 candidate = block + ivec2(phase & 3, (phase >> 2) & 3);
        if (any(greaterThanEqual(candidate, size))) continue;
        if (texelFetch(uAccumulation, candidate, 0).a <= 0.0) return false;
    }
    return true;
}

void main()
{
    ivec2 size = textureSize(uAccumulation, 0);
    ivec2 pixel = clamp(ivec2(vUv * vec2(size)), ivec2(0), size - ivec2(1));

    vec4 accumulated;
    if (uCameraMoving != 0) {
        accumulated = reconstructSparseSample(pixel, size);
    } else {
        accumulated = blockComplete(pixel, size)
            ? texture(uAccumulation, vUv)
            : reconstructSparseSample(pixel, size);
    }

    float samples = max(accumulated.a, 1.0);
    vec3 linear_color = max(accumulated.rgb / samples, vec3(0.0));
    linear_color *= max(uExposure, 0.0);
    vec3 mapped = linear_color / (vec3(1.0) + linear_color);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    outColor = vec4(mapped, 1.0);
}
)GLSL";

} // namespace Renderer::PathTracerShaders

#endif
