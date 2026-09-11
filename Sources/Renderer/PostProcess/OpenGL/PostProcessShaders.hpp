#ifndef HORSE_RENDERER_POST_PROCESS_OPENGL_SHADERS_HPP
#define HORSE_RENDERER_POST_PROCESS_OPENGL_SHADERS_HPP

namespace Renderer::PostProcessShaders {

inline constexpr const char *vertex = R"GLSL(
#version 120
varying vec2 vUv;
void main()
{
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
    vUv = gl_Vertex.xy * 0.5 + 0.5;
}
)GLSL";

inline constexpr const char *extract = R"GLSL(
#version 120
uniform sampler2D uScene;
uniform float uThreshold;
varying vec2 vUv;
void main()
{
    vec3 color = max(texture2D(uScene, vUv).rgb, vec3(0.0));
    float brightness = max(max(color.r, color.g), color.b);
    float weight = max(brightness - uThreshold, 0.0) / max(brightness, 1.0e-5);
    gl_FragColor = vec4(color * weight, 1.0);
}
)GLSL";

inline constexpr const char *blur = R"GLSL(
#version 120
uniform sampler2D uSource;
uniform vec2 uTexel;
uniform vec2 uDirection;
varying vec2 vUv;
void main()
{
    vec2 step_uv = uTexel * uDirection;
    vec3 color = texture2D(uSource, vUv).rgb * 0.227027;
    color += texture2D(uSource, vUv + step_uv * 1.384615).rgb * 0.316216;
    color += texture2D(uSource, vUv - step_uv * 1.384615).rgb * 0.316216;
    color += texture2D(uSource, vUv + step_uv * 3.230769).rgb * 0.070270;
    color += texture2D(uSource, vUv - step_uv * 3.230769).rgb * 0.070270;
    gl_FragColor = vec4(color, 1.0);
}
)GLSL";

inline constexpr const char *present = R"GLSL(
#version 120
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uVelocity;
uniform vec2 uTexel;
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uMotionBlurStrength;
uniform int uMotionBlurSamples;
uniform int uMotionBlurEnabled;
uniform int uFxaaEnabled;
varying vec2 vUv;

vec3 aces(vec3 x)
{
    return clamp(
        (x * (2.51 * x + 0.03)) /
        (x * (2.43 * x + 0.59) + 0.14),
        vec3(0.0), vec3(1.0)
    );
}

vec3 hdrAt(vec2 uv)
{
    vec2 sample_uv = clamp(uv, vec2(0.0), vec2(1.0));
    vec3 scene = max(texture2D(uScene, sample_uv).rgb, vec3(0.0));
    float bloom_intensity = max(uBloomIntensity, 0.0);
    if (bloom_intensity <= 0.0) return scene;
    vec3 bloom = max(texture2D(uBloom, sample_uv).rgb, vec3(0.0));
    return scene + bloom * bloom_intensity;
}

vec3 displayAt(vec2 uv)
{
    vec3 mapped = aces(hdrAt(uv) * max(uExposure, 0.0));
    return pow(mapped, vec3(1.0 / 2.2));
}

float luminance(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 motionBlurred(vec2 uv)
{
    if (uMotionBlurEnabled == 0 || uMotionBlurSamples <= 1)
        return hdrAt(uv);
    vec2 velocity = texture2D(uVelocity, uv).xy * max(uMotionBlurStrength, 0.0);
    int samples = clamp(uMotionBlurSamples, 1, 16);
    vec3 sum = vec3(0.0);
    float count = 0.0;
    for (int index = 0; index < 16; ++index) {
        if (index >= samples) break;
        float t = samples > 1 ? float(index) / float(samples - 1) - 0.5 : 0.0;
        sum += hdrAt(uv - velocity * t);
        count += 1.0;
    }
    return sum / max(count, 1.0);
}

vec3 displayMotion(vec2 uv)
{
    vec3 mapped = aces(motionBlurred(uv) * max(uExposure, 0.0));
    return pow(mapped, vec3(1.0 / 2.2));
}

void main()
{
    vec3 center = displayMotion(vUv);
    if (uFxaaEnabled == 0) {
        gl_FragColor = vec4(center, 1.0);
        return;
    }

    vec3 north = displayAt(vUv + vec2(0.0, uTexel.y));
    vec3 south = displayAt(vUv - vec2(0.0, uTexel.y));
    vec3 east = displayAt(vUv + vec2(uTexel.x, 0.0));
    vec3 west = displayAt(vUv - vec2(uTexel.x, 0.0));
    float lc = luminance(center);
    float ln = luminance(north);
    float ls = luminance(south);
    float le = luminance(east);
    float lw = luminance(west);
    float low = min(lc, min(min(ln, ls), min(le, lw)));
    float high = max(lc, max(max(ln, ls), max(le, lw)));
    if (high - low < max(0.0312, high * 0.125)) {
        gl_FragColor = vec4(center, 1.0);
        return;
    }

    float horizontal = abs(ln - ls);
    float vertical = abs(le - lw);
    vec3 edge = horizontal >= vertical
        ? (east + west) * 0.5
        : (north + south) * 0.5;
    gl_FragColor = vec4(mix(center, edge, 0.5), 1.0);
}
)GLSL";

} // namespace Renderer::PostProcessShaders

#endif
