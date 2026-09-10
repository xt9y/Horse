#ifndef HORSE_RENDERER_RASTERIZER_OPENGL_PBR_SHADERS_HPP
#define HORSE_RENDERER_RASTERIZER_OPENGL_PBR_SHADERS_HPP

namespace Renderer::RasterizerShaders {

inline constexpr const char *main_vertex = R"GLSL(
#version 120
uniform mat4 uModel;
uniform mat4 uPreviousModel;
uniform mat4 uCurrentViewProjection;
uniform mat4 uPreviousViewProjection;
uniform mat4 uNormalMatrix;
varying vec3 vWorldPosition;
varying vec3 vWorldNormal;
varying vec2 vUv;
varying vec2 vVelocity;
void main()
{
    vec4 world = uModel * gl_Vertex;
    vec4 current_clip = uCurrentViewProjection * world;
    vec4 previous_clip = uPreviousViewProjection * uPreviousModel * gl_Vertex;
    vec2 current_ndc = current_clip.xy / max(abs(current_clip.w), 1.0e-6);
    vec2 previous_ndc = previous_clip.xy / max(abs(previous_clip.w), 1.0e-6);
    vVelocity = (current_ndc - previous_ndc) * 0.5;
    vWorldPosition = world.xyz;
    vWorldNormal = normalize((uNormalMatrix * vec4(gl_Normal, 0.0)).xyz);
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = current_clip;
}
)GLSL";

inline constexpr const char *main_fragment = R"GLSL(
#version 120
uniform sampler2D uDiffuse;
uniform sampler2D uNormalMap;
uniform sampler2D uRoughnessMap;
uniform sampler2D uMetallicMap;
uniform sampler2D uAoMap;
uniform sampler2D uEmissiveMap;
uniform sampler2D uEnvironment;
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
uniform int uHasNormalMap;
uniform int uHasRoughnessMap;
uniform int uHasMetallicMap;
uniform int uHasAoMap;
uniform int uHasEmissiveMap;
uniform int uHasEnvironmentTexture;
uniform int uHasGi;
uniform int uLightType;
uniform int uHasShadow;
uniform int uFogMode;
uniform vec4 uBaseColor;
uniform float uRoughness;
uniform float uMetallic;
uniform float uAo;
uniform vec3 uEmissiveColor;
uniform float uEmissiveStrength;
uniform vec3 uCameraPosition;
uniform vec3 uLightPosition;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform float uLightRange;
uniform float uSpotInnerCos;
uniform float uSpotOuterCos;
uniform vec3 uEnvironmentAverage;
uniform float uEnvironmentIntensity;
uniform float uEnvironmentRotation;
uniform vec3 uAmbientColor;
uniform float uAmbientIntensity;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uGiMinimum;
uniform vec3 uGiMaximum;
uniform float uGiIntensity;
uniform float uShadowFar;
uniform float uShadowTexel;
uniform float uAlphaCutoff;
uniform mat4 uShadowMatrix0;
uniform mat4 uShadowMatrix1;
uniform mat4 uShadowMatrix2;
uniform mat4 uShadowMatrix3;
uniform mat4 uShadowMatrix4;
uniform mat4 uShadowMatrix5;
varying vec3 vWorldPosition;
varying vec3 vWorldNormal;
varying vec2 vUv;
varying vec2 vVelocity;
const float PI = 3.14159265358979323846;
const float SH_Y00 = 0.2820947918;
const float SH_Y1 = 0.4886025119;

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
float shadowCompare(int face, vec2 uv, float current, float bias)
{
    float half_texel = uShadowTexel * 0.5;
    vec2 safe_uv = clamp(uv, vec2(half_texel), vec2(1.0 - half_texel));
    return current - bias <= shadowDepth(face, safe_uv) ? 1.0 : 0.0;
}
float shadowVisibility(vec3 position, vec3 normal, vec3 light_direction)
{
    if (uHasShadow == 0 || uShadowFar <= 0.0) return 1.0;
    vec3 n = normalize(normal), l = normalize(light_direction);
    float slope = 1.0 - max(dot(n, l), 0.0);
    float normal_bias = max(0.001, uShadowFar * (0.0012 + slope * 0.0020));
    vec3 receiver_position = position + n * normal_bias;
    vec3 delta = receiver_position - uLightPosition;
    int face = shadowFace(delta);
    vec4 clip = shadowClip(face, receiver_position);
    if (clip.w <= 0.0) return 1.0;
    vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 1.0;
    float current = clamp(length(delta) / uShadowFar, 0.0, 1.0);
    float depth_bias = 0.0012 + slope * 0.0020;
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibility += shadowCompare(face, uv + vec2(float(x), float(y)) * uShadowTexel, current, depth_bias);
    return visibility / 9.0;
}
vec3 sampleGi(vec3 position, vec3 normal)
{
    if (uHasGi == 0) return vec3(0.0);
    vec3 extent = max(uGiMaximum - uGiMinimum, vec3(1.0e-6));
    vec3 uvw = clamp((position - uGiMinimum) / extent, vec3(0.0), vec3(1.0));
    vec3 c0 = texture3D(uGi0, uvw).rgb, c1 = texture3D(uGi1, uvw).rgb;
    vec3 c2 = texture3D(uGi2, uvw).rgb, c3 = texture3D(uGi3, uvw).rgb;
    vec3 n = normalize(normal);
    vec3 irradiance = c0 * (PI * SH_Y00);
    irradiance += c1 * ((2.0 * PI / 3.0) * SH_Y1 * n.x);
    irradiance += c2 * ((2.0 * PI / 3.0) * SH_Y1 * n.y);
    irradiance += c3 * ((2.0 * PI / 3.0) * SH_Y1 * n.z);
    return max(irradiance, vec3(0.0)) * max(uGiIntensity, 0.0);
}
vec2 environmentUv(vec3 direction)
{
    direction = normalize(direction);
    float c = cos(uEnvironmentRotation), s = sin(uEnvironmentRotation);
    direction.xz = mat2(c, -s, s, c) * direction.xz;
    return vec2(0.5 + atan(direction.z, direction.x) / (2.0 * PI),
        0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI);
}
vec3 environmentColor(vec3 direction, float roughness)
{
    vec3 average = max(uEnvironmentAverage, vec3(0.0));
    if (uHasEnvironmentTexture == 0) return average * uEnvironmentIntensity;
    vec3 sampled = pow(max(texture2D(uEnvironment, environmentUv(direction)).rgb, vec3(0.0)), vec3(2.2));
    return mix(sampled, average, clamp(roughness * roughness, 0.0, 1.0)) * uEnvironmentIntensity;
}
vec3 mappedNormal(vec3 normal)
{
    vec3 n = normalize(normal);
    if (uHasNormalMap == 0) return n;
    vec3 dp1 = dFdx(vWorldPosition), dp2 = dFdy(vWorldPosition);
    vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);
    float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(determinant) <= 1.0e-8) return n;
    vec3 tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) / determinant);
    tangent = normalize(tangent - n * dot(n, tangent));
    vec3 bitangent = normalize(cross(n, tangent));
    if (determinant < 0.0) bitangent = -bitangent;
    vec3 sampled = texture2D(uNormalMap, vUv).xyz * 2.0 - 1.0;
    return normalize(tangent * sampled.x + bitangent * sampled.y + n * sampled.z);
}
float distributionGGX(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness, a2 = a * a;
    float nh = max(dot(n, h), 0.0);
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 1.0e-6);
}
float geometrySchlickGGX(float nv, float roughness)
{
    float r = roughness + 1.0, k = r * r * 0.125;
    return nv / max(nv * (1.0 - k) + k, 1.0e-6);
}
float geometrySmith(vec3 n, vec3 v, vec3 l, float roughness)
{
    return geometrySchlickGGX(max(dot(n, v), 0.0), roughness) * geometrySchlickGGX(max(dot(n, l), 0.0), roughness);
}
vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}
vec3 pbrDirect(vec3 albedo, float roughness, float metallic, vec3 normal, vec3 view_direction, vec3 light_direction, vec3 incoming)
{
    vec3 n = normalize(normal), v = normalize(view_direction), l = normalize(light_direction), h = normalize(v + l);
    float nl = max(dot(n, l), 0.0), nv = max(dot(n, v), 0.0);
    if (nl <= 0.0 || nv <= 0.0) return vec3(0.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    float d = distributionGGX(n, h, roughness), g = geometrySmith(n, v, l, roughness);
    vec3 specular = d * g * f / max(4.0 * nv * nl, 1.0e-5);
    vec3 diffuse = (vec3(1.0) - f) * (1.0 - metallic) * albedo / PI;
    return (diffuse + specular) * incoming * nl;
}
float lightAttenuation(float distance_to_light)
{
    if (uLightRange <= 0.0) return 1.0;
    float ratio = clamp(distance_to_light / uLightRange, 0.0, 1.0);
    float falloff = 1.0 - ratio * ratio;
    return falloff * falloff;
}
float spotAttenuation(vec3 light_direction_to_surface)
{
    float cosine = dot(normalize(uLightDirection), normalize(light_direction_to_surface));
    return clamp((cosine - uSpotOuterCos) / max(uSpotInnerCos - uSpotOuterCos, 1.0e-5), 0.0, 1.0);
}
float fogVisibility(float distance_to_camera)
{
    if (uFogMode == 1) return clamp((uFogEnd - distance_to_camera) / max(uFogEnd - uFogStart, 1.0e-5), 0.0, 1.0);
    if (uFogMode == 2) return exp(-max(uFogDensity, 0.0) * distance_to_camera);
    return 1.0;
}
void main()
{
    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, vUv) : vec4(1.0);
    float alpha = clamp(uBaseColor.a * texel.a, 0.0, 1.0);
    if (alpha < uAlphaCutoff) discard;
    vec3 albedo = max(uBaseColor.rgb, vec3(0.0)) * pow(max(texel.rgb, vec3(0.0)), vec3(2.2));
    float roughness = max(uRoughness * (uHasRoughnessMap != 0 ? texture2D(uRoughnessMap, vUv).r : 1.0), 0.04);
    float metallic = clamp(uMetallic * (uHasMetallicMap != 0 ? texture2D(uMetallicMap, vUv).r : 1.0), 0.0, 1.0);
    float ao = clamp(uAo * (uHasAoMap != 0 ? texture2D(uAoMap, vUv).r : 1.0), 0.0, 1.0);
    vec3 normal = mappedNormal(vWorldNormal);
    vec3 view_direction = normalize(uCameraPosition - vWorldPosition);
    vec3 direct = vec3(0.0);
    if (uLightType != 0 && uLightIntensity > 0.0) {
        vec3 light_direction = vec3(0.0, 1.0, 0.0);
        vec3 incoming = max(uLightColor, vec3(0.0)) * max(uLightIntensity, 0.0);
        float visibility = 1.0;
        if (uLightType == 1 || uLightType == 3) {
            vec3 to_light = uLightPosition - vWorldPosition;
            float distance_squared = max(dot(to_light, to_light), 1.0e-4);
            float distance_to_light = sqrt(distance_squared);
            light_direction = to_light / distance_to_light;
            incoming *= lightAttenuation(distance_to_light) / distance_squared;
            if (uLightType == 3) incoming *= spotAttenuation(vWorldPosition - uLightPosition);
            visibility = shadowVisibility(vWorldPosition, normal, light_direction);
        } else if (uLightType == 2) {
            light_direction = normalize(-uLightDirection);
        }
        direct = pbrDirect(albedo, roughness, metallic, normal, view_direction, light_direction, incoming) * visibility;
    }
    vec3 indirect = albedo * (1.0 - metallic) * ao * sampleGi(vWorldPosition, normal) / PI;
    vec3 ambient = max(uAmbientColor, vec3(0.0)) * max(uAmbientIntensity, 0.0) * albedo * ao;
    vec3 reflected = reflect(-view_direction, normal);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 ibl = max(uEnvironmentAverage, vec3(0.0)) * max(uEnvironmentIntensity, 0.0) *
        albedo * (1.0 - metallic) * ao / PI +
        environmentColor(reflected, roughness) * fresnelSchlick(max(dot(normal, view_direction), 0.0), f0) * ao;
    vec3 emissive = max(uEmissiveColor, vec3(0.0)) * max(uEmissiveStrength, 0.0);
    if (uHasEmissiveMap != 0) emissive *= pow(max(texture2D(uEmissiveMap, vUv).rgb, vec3(0.0)), vec3(2.2));
    vec3 linear_color = max(direct + indirect + ambient + ibl + emissive, vec3(0.0));
    linear_color = mix(max(uFogColor, vec3(0.0)), linear_color, fogVisibility(length(uCameraPosition - vWorldPosition)));
    gl_FragData[0] = vec4(linear_color, alpha);
    gl_FragData[1] = vec4(vVelocity, 0.0, 1.0);
}
)GLSL";

inline constexpr const char *sky_vertex = R"GLSL(
#version 120
varying vec2 vUv;
void main()
{
    gl_Position = vec4(gl_Vertex.xy, 0.999999, 1.0);
    vUv = gl_Vertex.xy * 0.5 + 0.5;
}
)GLSL";

inline constexpr const char *sky_fragment = R"GLSL(
#version 120
uniform sampler2D uEnvironment;
uniform int uHasEnvironmentTexture;
uniform vec3 uSkyColor;
uniform float uEnvironmentIntensity;
uniform float uEnvironmentRotation;
uniform vec3 uCameraForward;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uPreviousCameraForward;
uniform vec3 uPreviousCameraRight;
uniform vec3 uPreviousCameraUp;
uniform float uTanHalfFov;
uniform float uAspect;
varying vec2 vUv;
const float PI = 3.14159265358979323846;
vec2 environmentUv(vec3 direction)
{
    direction = normalize(direction);
    float c = cos(uEnvironmentRotation), s = sin(uEnvironmentRotation);
    direction.xz = mat2(c, -s, s, c) * direction.xz;
    return vec2(0.5 + atan(direction.z, direction.x) / (2.0 * PI),
        0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI);
}
void main()
{
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 direction = normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * uTanHalfFov) + uCameraUp * (ndc.y * uTanHalfFov));
    vec3 linear_color = max(uSkyColor, vec3(0.0)) * max(uEnvironmentIntensity, 0.0);
    if (uHasEnvironmentTexture != 0)
        linear_color = pow(max(texture2D(uEnvironment, environmentUv(direction)).rgb, vec3(0.0)), vec3(2.2)) * max(uEnvironmentIntensity, 0.0);
    float previous_z = max(dot(direction, uPreviousCameraForward), 1.0e-5);
    vec2 previous_ndc = vec2(
        dot(direction, uPreviousCameraRight) / (previous_z * uAspect * uTanHalfFov),
        dot(direction, uPreviousCameraUp) / (previous_z * uTanHalfFov)
    );
    vec2 velocity = (ndc - previous_ndc) * 0.5;
    gl_FragData[0] = vec4(linear_color, 1.0);
    gl_FragData[1] = vec4(velocity, 0.0, 1.0);
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
uniform float uAlphaCutoff;
varying vec3 vWorldPosition;
varying vec2 vUv;
vec3 encodeDepth(float depth)
{
    vec3 encoded = fract(depth * vec3(1.0, 255.0, 65025.0));
    encoded -= encoded.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
    return encoded;
}
void main()
{
    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, vUv) : vec4(1.0);
    if (clamp(uBaseAlpha * texel.a, 0.0, 1.0) < uAlphaCutoff) discard;
    float depth = clamp(length(vWorldPosition - uLightPosition) / max(uShadowFar, 1.0e-4), 0.0, 0.999999);
    gl_FragColor = vec4(encodeDepth(depth), 1.0);
}
)GLSL";

} // namespace Renderer::RasterizerShaders

#endif
