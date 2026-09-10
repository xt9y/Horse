#ifndef HORSE_RENDERER_UPSCALE_OPENGL_SHADERS_HPP
#define HORSE_RENDERER_UPSCALE_OPENGL_SHADERS_HPP

namespace Renderer::Upscale::OpenGLShaders {

inline constexpr const char *vertex = R"GLSL(
#version 120

varying vec2 vUv;

void main()
{
    gl_Position = gl_Vertex;
    vUv = gl_MultiTexCoord0.xy;
}
)GLSL";

inline constexpr const char *fragment = R"GLSL(
#version 120

uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform sampler2D uFullDepth;
uniform sampler2D uEffect;
uniform sampler2D uHistory;
uniform vec2 uSourceSize;
uniform vec2 uEffectSize;
uniform float uNearPlane;
uniform float uDepthThreshold;
uniform float uTemporalWeight;
uniform int uDepthAware;
uniform int uHasEffect;
uniform int uHistoryValid;

varying vec2 vUv;

float linearDepth(float depth)
{
    return uNearPlane / max(1.0 - depth, 1.0e-6);
}

float relativeDepthDifference(float a, float b)
{
    float linear_a = linearDepth(a);
    float linear_b = linearDepth(b);
    return abs(linear_a - linear_b) / max(linear_a, uNearPlane);
}

vec2 nearestDepthUv(vec2 uv, vec2 texture_size, float reference_depth)
{
    vec2 size = max(texture_size, vec2(1.0));
    vec2 position = uv * size - vec2(0.5);
    vec2 base = floor(position);
    vec2 best_uv = (base + vec2(0.5)) / size;
    float best_difference = 1.0e30;

    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec2 candidate = (base + vec2(float(x), float(y)) + vec2(0.5)) / size;
            candidate = clamp(candidate, vec2(0.0), vec2(0.999999));
            float candidate_depth = texture2D(uDepth, candidate).r;
            float difference = relativeDepthDifference(reference_depth, candidate_depth);
            if (difference < best_difference) {
                best_difference = difference;
                best_uv = candidate;
            }
        }
    }
    return best_uv;
}

void main()
{
    float full_depth = texture2D(uFullDepth, vUv).r;
    vec2 color_uv = vUv;
    if (uDepthAware != 0) {
        vec2 candidate = nearestDepthUv(vUv, uSourceSize, full_depth);
        float candidate_depth = texture2D(uDepth, candidate).r;
        if (relativeDepthDifference(full_depth, candidate_depth) <= uDepthThreshold)
            color_uv = candidate;
    }

    vec4 base = texture2D(uColor, color_uv);
    vec3 linear_color = pow(max(base.rgb, vec3(0.0)), vec3(2.2));

    if (uHasEffect != 0) {
        vec2 effect_uv = vUv;
        if (uDepthAware != 0) effect_uv = nearestDepthUv(vUv, uEffectSize, full_depth);
        vec4 effect = texture2D(uEffect, effect_uv);
        linear_color = linear_color * clamp(effect.a, 0.0, 1.0) + max(effect.rgb, vec3(0.0));
    }

    vec3 mapped = pow(clamp(linear_color, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
    vec4 current_value = vec4(mapped, base.a);
    if (uHistoryValid != 0) {
        vec4 history_value = texture2D(uHistory, vUv);
        current_value = mix(current_value, history_value, clamp(uTemporalWeight, 0.0, 1.0));
    }
    gl_FragColor = current_value;
}
)GLSL";

} // namespace Renderer::Upscale::OpenGLShaders

#endif
