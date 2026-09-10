#ifndef HORSE_RENDERER_HORIZON_GI_OPENGL_SHADERS_HPP
#define HORSE_RENDERER_HORIZON_GI_OPENGL_SHADERS_HPP

namespace Renderer::HorizonGI::OpenGLShaders {

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

uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;
uniform sampler2D uHistory;
uniform vec2 uSourceSize;
uniform float uNearPlane;
uniform float uTanHalfFov;
uniform float uAspect;
uniform float uRadius;
uniform float uThickness;
uniform float uAoStrength;
uniform float uIndirectStrength;
uniform float uIntensity;
uniform float uTemporalWeight;
uniform int uDirections;
uniform int uSteps;
uniform int uIndirectEnabled;
uniform int uHistoryValid;

varying vec2 vUv;

const float PI = 3.14159265358979323846;

float depthAt(vec2 uv)
{
    return texture2D(uSceneDepth, clamp(uv, vec2(0.0), vec2(0.999999))).r;
}

float linearDepth(float depth)
{
    return uNearPlane / max(1.0 - depth, 1.0e-6);
}

vec3 viewPosition(vec2 uv)
{
    float distance_to_plane = linearDepth(depthAt(uv));
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(
        ndc.x * distance_to_plane * uAspect * uTanHalfFov,
        ndc.y * distance_to_plane * uTanHalfFov,
        -distance_to_plane
    );
}

vec3 viewNormal(vec2 uv, vec3 center)
{
    vec2 texel = 1.0 / max(uSourceSize, vec2(1.0));
    vec3 left_position = viewPosition(uv - vec2(texel.x, 0.0));
    vec3 right_position = viewPosition(uv + vec2(texel.x, 0.0));
    vec3 down_position = viewPosition(uv - vec2(0.0, texel.y));
    vec3 up_position = viewPosition(uv + vec2(0.0, texel.y));

    vec3 horizontal = length(right_position - center) < length(center - left_position)
        ? right_position - center
        : center - left_position;
    vec3 vertical = length(up_position - center) < length(center - down_position)
        ? up_position - center
        : center - down_position;
    vec3 normal = normalize(cross(horizontal, vertical));
    if (normal.z < 0.0) normal = -normal;
    return normal;
}

vec3 linearColor(vec2 uv)
{
    return pow(max(texture2D(uSceneColor, uv).rgb, vec3(0.0)), vec3(2.2));
}

void main()
{
    float center_depth = depthAt(vUv);
    if (center_depth >= 0.999999) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 center = viewPosition(vUv);
    vec3 normal = viewNormal(vUv, center);
    float center_distance = max(-center.z, uNearPlane);
    float projected_radius = max(
        uRadius * (0.5 * uSourceSize.y) / max(center_distance * uTanHalfFov, 1.0e-5),
        1.0
    );

    float occlusion = 0.0;
    vec3 indirect = vec3(0.0);
    float direction_count = float(max(uDirections, 1));
    float step_count = float(max(uSteps, 1));

    for (int direction_index = 0; direction_index < 8; ++direction_index) {
        if (direction_index >= uDirections) break;
        float angle = (2.0 * PI * (float(direction_index) + 0.5)) / direction_count;
        vec2 screen_direction = vec2(cos(angle), sin(angle));
        float horizon = 0.0;
        vec3 horizon_color = vec3(0.0);

        for (int step_index = 1; step_index <= 16; ++step_index) {
            if (step_index > uSteps) break;
            float step_fraction = float(step_index) / step_count;
            vec2 sample_uv = vUv + screen_direction *
                (projected_radius * step_fraction) / max(uSourceSize, vec2(1.0));
            if (sample_uv.x <= 0.0 || sample_uv.x >= 1.0 ||
                sample_uv.y <= 0.0 || sample_uv.y >= 1.0) continue;

            float sample_depth = depthAt(sample_uv);
            if (sample_depth >= 0.999999) continue;
            vec3 delta = viewPosition(sample_uv) - center;
            float distance_to_sample = length(delta);
            if (distance_to_sample <= 1.0e-5 || distance_to_sample > uRadius + uThickness)
                continue;

            vec3 sample_direction = delta / distance_to_sample;
            float falloff = 1.0 - clamp(distance_to_sample / max(uRadius, 1.0e-5), 0.0, 1.0);
            float thickness_bias = uThickness / max(distance_to_sample, 1.0e-4);
            float candidate = max(dot(normal, sample_direction) - thickness_bias, 0.0) * falloff;
            if (candidate > horizon) {
                horizon = candidate;
                horizon_color = linearColor(sample_uv);
            }
        }

        occlusion += horizon;
        indirect += horizon_color * horizon;
    }

    float ao = clamp(1.0 - (occlusion / direction_count) * uAoStrength, 0.0, 1.0);
    indirect = indirect / direction_count;
    if (uIndirectEnabled == 0) indirect = vec3(0.0);
    indirect *= max(uIndirectStrength, 0.0) * max(uIntensity, 0.0);

    vec4 current_value = vec4(indirect, ao);
    if (uHistoryValid != 0) {
        vec4 history_value = texture2D(uHistory, vUv);
        current_value = mix(current_value, history_value, clamp(uTemporalWeight, 0.0, 1.0));
    }
    gl_FragColor = current_value;
}
)GLSL";

} // namespace Renderer::HorizonGI::OpenGLShaders

#endif
