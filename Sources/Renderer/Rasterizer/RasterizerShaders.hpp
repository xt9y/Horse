#ifndef RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP

namespace Renderer::RasterizerShaders {

inline constexpr const char *main_vertex = R"GLSL(
#version 120

uniform mat4 uModel;
uniform mat4 uNormalMatrix;

varying vec3 vWorldPosition;
varying vec3 vWorldNormal;
varying vec2 vUv;

void main()
{
    vec4 world = uModel * gl_Vertex;
    vWorldPosition = world.xyz;
    vWorldNormal = normalize((uNormalMatrix * vec4(gl_Normal, 0.0)).xyz);
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";

inline constexpr const char *main_fragment = R"GLSL(
#version 120

uniform sampler2D uDiffuse;
uniform sampler3D uGi0;
uniform sampler3D uGi1;
uniform sampler3D uGi2;
uniform sampler3D uGi3;
uniform sampler2D uShadow0;
uniform sampler2D uShadow1;
uniform sampler2D uShadow2;
uniform sampler2D uShadow3;
uniform sampler2D uShadow4;
uniform sampler2D uShadow5;

uniform int uHasTexture;
uniform int uHasGi;
uniform int uLightType;
uniform int uHasShadow;
uniform vec4 uBaseColor;
uniform vec3 uLightPosition;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform vec3 uGiMinimum;
uniform vec3 uGiMaximum;
uniform float uGiIntensity;
uniform float uShadowFar;
uniform float uShadowTexel;
uniform mat4 uShadowMatrix0;
uniform mat4 uShadowMatrix1;
uniform mat4 uShadowMatrix2;
uniform mat4 uShadowMatrix3;
uniform mat4 uShadowMatrix4;
uniform mat4 uShadowMatrix5;

varying vec3 vWorldPosition;
varying vec3 vWorldNormal;
varying vec2 vUv;

const float PI = 3.14159265358979323846;
const float SH_Y00 = 0.2820947918;
const float SH_Y1 = 0.4886025119;
const float ALPHA_CUTOFF = 0.5;

float decodeDepth(vec3 encoded)
{
    return dot(encoded, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}

int shadowFace(vec3 delta)
{
    vec3 a = abs(delta);
    if (a.x >= a.y && a.x >= a.z) return delta.x >= 0.0 ? 0 : 1;
    if (a.y >= a.z) return delta.y >= 0.0 ? 2 : 3;
    return delta.z >= 0.0 ? 4 : 5;
}

vec4 shadowClip(int face, vec3 position)
{
    vec4 world = vec4(position, 1.0);
    if (face == 0) return uShadowMatrix0 * world;
    if (face == 1) return uShadowMatrix1 * world;
    if (face == 2) return uShadowMatrix2 * world;
    if (face == 3) return uShadowMatrix3 * world;
    if (face == 4) return uShadowMatrix4 * world;
    return uShadowMatrix5 * world;
}

float shadowDepth(int face, vec2 uv)
{
    if (face == 0) return decodeDepth(texture2D(uShadow0, uv).rgb);
    if (face == 1) return decodeDepth(texture2D(uShadow1, uv).rgb);
    if (face == 2) return decodeDepth(texture2D(uShadow2, uv).rgb);
    if (face == 3) return decodeDepth(texture2D(uShadow3, uv).rgb);
    if (face == 4) return decodeDepth(texture2D(uShadow4, uv).rgb);
    return decodeDepth(texture2D(uShadow5, uv).rgb);
}

float shadowVisibility(vec3 position, vec3 normal, vec3 light_direction)
{
    if (uHasShadow == 0 || uShadowFar <= 0.0) return 1.0;

    vec3 n = normalize(normal);
    vec3 l = normalize(light_direction);
    float slope = 1.0 - max(dot(n, l), 0.0);
    float normal_bias = max(0.001, uShadowFar * (0.0015 + slope * 0.0025));
    vec3 receiver_position = position + n * normal_bias;

    vec3 delta = receiver_position - uLightPosition;
    int face = shadowFace(delta);
    vec4 clip = shadowClip(face, receiver_position);
    if (clip.w <= 0.0) return 1.0;

    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 1.0;

    float current = clamp(length(delta) / uShadowFar, 0.0, 1.0);
    float depth_bias = 0.0015 + slope * 0.0025;

    vec2 o = vec2(uShadowTexel, uShadowTexel);
    float visibility = 0.0;
    visibility += current - depth_bias <= shadowDepth(face, uv + vec2(-o.x, -o.y)) ? 1.0 : 0.0;
    visibility += current - depth_bias <= shadowDepth(face, uv + vec2( o.x, -o.y)) ? 1.0 : 0.0;
    visibility += current - depth_bias <= shadowDepth(face, uv + vec2(-o.x,  o.y)) ? 1.0 : 0.0;
    visibility += current - depth_bias <= shadowDepth(face, uv + vec2( o.x,  o.y)) ? 1.0 : 0.0;
    return visibility * 0.25;
}

vec3 sampleGi(vec3 position, vec3 normal)
{
    if (uHasGi == 0) return vec3(0.0);
    vec3 extent = max(uGiMaximum - uGiMinimum, vec3(1.0e-6));
    vec3 uvw = clamp((position - uGiMinimum) / extent, vec3(0.0), vec3(1.0));
    vec3 c0 = texture3D(uGi0, uvw).rgb;
    vec3 c1 = texture3D(uGi1, uvw).rgb;
    vec3 c2 = texture3D(uGi2, uvw).rgb;
    vec3 c3 = texture3D(uGi3, uvw).rgb;
    vec3 n = normalize(normal);
    vec3 irradiance = c0 * (PI * SH_Y00);
    irradiance += c1 * ((2.0 * PI / 3.0) * SH_Y1 * n.x);
    irradiance += c2 * ((2.0 * PI / 3.0) * SH_Y1 * n.y);
    irradiance += c3 * ((2.0 * PI / 3.0) * SH_Y1 * n.z);
    return max(irradiance, vec3(0.0)) * max(uGiIntensity, 0.0);
}

void main()
{
    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, vUv) : vec4(1.0);
    float alpha = clamp(uBaseColor.a * texel.a, 0.0, 1.0);
    if (alpha < ALPHA_CUTOFF) discard;

    vec3 normal = normalize(vWorldNormal);
    vec3 direct = vec3(0.0);

    if (uLightType == 1 && uLightIntensity > 0.0) {
        vec3 to_light = uLightPosition - vWorldPosition;
        float distance_squared = max(dot(to_light, to_light), 1.0e-4);
        vec3 light_direction = to_light * inversesqrt(distance_squared);
        float cosine = max(dot(normal, light_direction), 0.0);
        float visibility = shadowVisibility(vWorldPosition, normal, light_direction);
        direct = max(uLightColor, vec3(0.0)) * max(uLightIntensity, 0.0) *
            (cosine * visibility / distance_squared);
    } else if (uLightType == 2 && uLightIntensity > 0.0) {
        vec3 light_direction = normalize(uLightPosition);
        float cosine = max(dot(normal, light_direction), 0.0);
        direct = max(uLightColor, vec3(0.0)) * max(uLightIntensity, 0.0) * cosine;
    }

    vec3 indirect = sampleGi(vWorldPosition, normal);
    vec3 linear_texture = pow(max(texel.rgb, vec3(0.0)), vec3(2.2));
    vec3 albedo = max(uBaseColor.rgb * linear_texture, vec3(0.0));
    vec3 linear_color = albedo * (direct + indirect) * (1.0 / PI);
    linear_color += albedo * 0.025;
    vec3 mapped = linear_color / (vec3(1.0) + linear_color);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    gl_FragColor = vec4(mapped, alpha);
}
)GLSL";

inline constexpr const char *shadow_vertex = R"GLSL(
#version 120

uniform mat4 uModel;
uniform vec3 uLightPosition;

varying vec3 vWorldPosition;
varying vec2 vUv;

void main()
{
    vec4 world = uModel * gl_Vertex;
    vWorldPosition = world.xyz;
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";

inline constexpr const char *shadow_fragment = R"GLSL(
#version 120

uniform sampler2D uDiffuse;
uniform int uHasTexture;
uniform float uBaseAlpha;
uniform vec3 uLightPosition;
uniform float uShadowFar;

varying vec3 vWorldPosition;
varying vec2 vUv;

const float ALPHA_CUTOFF = 0.5;

vec3 encodeDepth(float depth)
{
    vec3 encoded = fract(depth * vec3(1.0, 255.0, 65025.0));
    encoded -= encoded.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
    return encoded;
}

void main()
{
    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, vUv) : vec4(1.0);
    if (clamp(uBaseAlpha * texel.a, 0.0, 1.0) < ALPHA_CUTOFF) discard;
    float depth = clamp(length(vWorldPosition - uLightPosition) / max(uShadowFar, 1.0e-4), 0.0, 0.999999);
    gl_FragColor = vec4(encodeDepth(depth), 1.0);
}
)GLSL";

} // namespace Renderer::RasterizerShaders

#endif