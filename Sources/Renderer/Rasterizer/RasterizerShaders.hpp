#ifndef RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_SHADERS_HPP

#include <string>

#define main_fragment horse_pbr_main_fragment
#define sky_fragment horse_pbr_sky_fragment
#define shadow_fragment horse_pbr_shadow_fragment
#include "Renderer/Rasterizer/OpenGL/PbrRasterizerShaders.hpp"
#undef shadow_fragment
#undef sky_fragment
#undef main_fragment

namespace Renderer::RasterizerShaders {
namespace {

inline void replaceOnce(std::string& source, const char *from, const char *to)
{
    const std::size_t position = source.find(from);
    if (position == std::string::npos) return;
    source.replace(position, std::char_traits<char>::length(from), to);
}

inline std::string compatibleMainFragment()
{
    std::string source = horse_pbr_main_fragment;
    replaceOnce(source, "uniform sampler2D uEnvironment;\n", "");
    replaceOnce(
        source,
        "uniform float uEmissiveStrength;\n",
        "uniform float uEmissiveStrength;\n"
        "uniform float uNormalScale;\n"
        "uniform int uUnlit;\n"
        "uniform float uIor;\n"
        "uniform float uSpecular;\n"
        "uniform vec3 uSpecularColor;\n"
        "uniform float uClearcoat;\n"
        "uniform float uClearcoatRoughness;\n"
        "uniform vec3 uSheenColor;\n"
        "uniform float uSheenRoughness;\n"
        "uniform float uTransmission;\n"
        "uniform float uThickness;\n"
        "uniform float uAttenuationDistance;\n"
        "uniform vec3 uAttenuationColor;\n"
        "uniform float uDiffuseTransmission;\n"
        "uniform vec3 uDiffuseTransmissionColor;\n"
        "uniform vec4 uBaseUvTransform;\n"
        "uniform float uBaseUvRotation;\n"
        "uniform vec4 uNormalUvTransform;\n"
        "uniform float uNormalUvRotation;\n"
        "uniform vec4 uRoughnessUvTransform;\n"
        "uniform float uRoughnessUvRotation;\n"
        "uniform vec4 uMetallicUvTransform;\n"
        "uniform float uMetallicUvRotation;\n"
        "uniform vec4 uAoUvTransform;\n"
        "uniform float uAoUvRotation;\n"
        "uniform vec4 uEmissiveUvTransform;\n"
        "uniform float uEmissiveUvRotation;\n"
    );
    replaceOnce(
        source,
        "vec3 mappedNormal(vec3 normal)\n{\n",
        "vec2 transformedUv(vec2 uv, vec4 transform, float rotation)\n"
        "{\n"
        "    vec2 scaled = uv * transform.zw;\n"
        "    float c = cos(rotation), s = sin(rotation);\n"
        "    return transform.xy + vec2(c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y);\n"
        "}\n"
        "vec3 dielectricF0()\n"
        "{\n"
        "    float ior = max(uIor, 1.0001);\n"
        "    float f = (ior - 1.0) / (ior + 1.0);\n"
        "    return max(uSpecularColor, vec3(0.0)) * max(uSpecular, 0.0) * (f * f);\n"
        "}\n"
        "vec3 mappedNormal(vec3 normal)\n{\n"
    );
    replaceOnce(
        source,
        "    vec3 dp1 = dFdx(vWorldPosition), dp2 = dFdy(vWorldPosition);\n"
        "    vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);\n",
        "    vec2 normal_uv = transformedUv(vUv, uNormalUvTransform, uNormalUvRotation);\n"
        "    vec3 dp1 = dFdx(vWorldPosition), dp2 = dFdy(vWorldPosition);\n"
        "    vec2 duv1 = dFdx(normal_uv), duv2 = dFdy(normal_uv);\n"
    );
    replaceOnce(
        source,
        "    vec3 sampled = texture2D(uNormalMap, vUv).xyz * 2.0 - 1.0;\n",
        "    vec3 sampled = texture2D(uNormalMap, normal_uv).xyz * 2.0 - 1.0;\n"
        "    sampled.xy *= uNormalScale;\n"
    );
    replaceOnce(
        source,
        "vec3 pbrDirect(vec3 albedo, float roughness, float metallic, vec3 normal, vec3 view_direction, vec3 light_direction, vec3 incoming)\n"
        "{\n"
        "    vec3 n = normalize(normal), v = normalize(view_direction), l = normalize(light_direction), h = normalize(v + l);\n"
        "    float nl = max(dot(n, l), 0.0), nv = max(dot(n, v), 0.0);\n"
        "    if (nl <= 0.0 || nv <= 0.0) return vec3(0.0);\n"
        "    vec3 f0 = mix(vec3(0.04), albedo, metallic);\n"
        "    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n"
        "    float d = distributionGGX(n, h, roughness), g = geometrySmith(n, v, l, roughness);\n"
        "    vec3 specular = d * g * f / max(4.0 * nv * nl, 1.0e-5);\n"
        "    vec3 diffuse = (vec3(1.0) - f) * (1.0 - metallic) * albedo / PI;\n"
        "    return (diffuse + specular) * incoming * nl;\n"
        "}\n",
        "vec3 pbrDirect(vec3 albedo, float roughness, float metallic, vec3 normal, vec3 view_direction, vec3 light_direction, vec3 incoming)\n"
        "{\n"
        "    vec3 n = normalize(normal), v = normalize(view_direction), l = normalize(light_direction), h = normalize(v + l);\n"
        "    float nl = max(dot(n, l), 0.0), nv = max(dot(n, v), 0.0);\n"
        "    if (nl <= 0.0 || nv <= 0.0) return vec3(0.0);\n"
        "    vec3 f0 = mix(dielectricF0(), albedo, metallic);\n"
        "    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);\n"
        "    float d = distributionGGX(n, h, roughness), g = geometrySmith(n, v, l, roughness);\n"
        "    vec3 specular_lobe = d * g * f / max(4.0 * nv * nl, 1.0e-5);\n"
        "    vec3 diffuse = (vec3(1.0) - f) * (1.0 - metallic) * albedo / PI;\n"
        "    if (uClearcoat > 0.0) {\n"
        "        float coat_roughness = max(uClearcoatRoughness, 0.04);\n"
        "        float coat_d = distributionGGX(n, h, coat_roughness);\n"
        "        float coat_g = geometrySmith(n, v, l, coat_roughness);\n"
        "        vec3 coat_f = fresnelSchlick(max(dot(h, v), 0.0), vec3(0.04));\n"
        "        specular_lobe += clamp(uClearcoat, 0.0, 1.0) * coat_d * coat_g * coat_f / max(4.0 * nv * nl, 1.0e-5);\n"
        "    }\n"
        "    float sheen_grazing = pow(clamp(1.0 - max(dot(n, h), 0.0), 0.0, 1.0), 1.0 + 4.0 * clamp(uSheenRoughness, 0.0, 1.0));\n"
        "    vec3 sheen = max(uSheenColor, vec3(0.0)) * sheen_grazing;\n"
        "    return (diffuse + specular_lobe + sheen) * incoming * nl;\n"
        "}\n"
    );
    replaceOnce(
        source,
        "    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, vUv) : vec4(1.0);\n",
        "    vec2 base_uv = transformedUv(vUv, uBaseUvTransform, uBaseUvRotation);\n"
        "    vec2 roughness_uv = transformedUv(vUv, uRoughnessUvTransform, uRoughnessUvRotation);\n"
        "    vec2 metallic_uv = transformedUv(vUv, uMetallicUvTransform, uMetallicUvRotation);\n"
        "    vec2 ao_uv = transformedUv(vUv, uAoUvTransform, uAoUvRotation);\n"
        "    vec2 emissive_uv = transformedUv(vUv, uEmissiveUvTransform, uEmissiveUvRotation);\n"
        "    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, base_uv) : vec4(1.0);\n"
    );
    replaceOnce(
        source,
        "    float roughness = max(uRoughness * (uHasRoughnessMap != 0 ? texture2D(uRoughnessMap, vUv).r : 1.0), 0.04);\n"
        "    float metallic = clamp(uMetallic * (uHasMetallicMap != 0 ? texture2D(uMetallicMap, vUv).r : 1.0), 0.0, 1.0);\n"
        "    float ao = clamp(uAo * (uHasAoMap != 0 ? texture2D(uAoMap, vUv).r : 1.0), 0.0, 1.0);\n",
        "    float roughness = max(uRoughness * (uHasRoughnessMap != 0 ? texture2D(uRoughnessMap, roughness_uv).r : 1.0), 0.04);\n"
        "    float metallic = clamp(uMetallic * (uHasMetallicMap != 0 ? texture2D(uMetallicMap, metallic_uv).r : 1.0), 0.0, 1.0);\n"
        "    float ao = clamp(uAo * (uHasAoMap != 0 ? texture2D(uAoMap, ao_uv).r : 1.0), 0.0, 1.0);\n"
    );
    replaceOnce(
        source,
        "    vec3 indirect = albedo * (1.0 - metallic) * ao * sampleGi(vWorldPosition, normal) / PI;\n"
        "    vec3 ambient = max(uAmbientColor, vec3(0.0)) * max(uAmbientIntensity, 0.0) * albedo * ao;\n"
        "    vec3 reflected = reflect(-view_direction, normal);\n"
        "    vec3 f0 = mix(vec3(0.04), albedo, metallic);\n"
        "    vec3 ibl = max(uEnvironmentAverage, vec3(0.0)) * max(uEnvironmentIntensity, 0.0) *\n"
        "        albedo * (1.0 - metallic) * ao / PI +\n"
        "        environmentColor(reflected, roughness) * fresnelSchlick(max(dot(normal, view_direction), 0.0), f0) * ao;\n"
        "    vec3 emissive = max(uEmissiveColor, vec3(0.0)) * max(uEmissiveStrength, 0.0);\n"
        "    if (uHasEmissiveMap != 0) emissive *= pow(max(texture2D(uEmissiveMap, vUv).rgb, vec3(0.0)), vec3(2.2));\n"
        "    vec3 linear_color = max(direct + indirect + ambient + ibl + emissive, vec3(0.0));\n",
        "    vec3 indirect = albedo * (1.0 - metallic) * ao * sampleGi(vWorldPosition, normal) / PI;\n"
        "    vec3 ambient = max(uAmbientColor, vec3(0.0)) * max(uAmbientIntensity, 0.0) * albedo * ao;\n"
        "    vec3 reflected = reflect(-view_direction, normal);\n"
        "    vec3 f0 = mix(dielectricF0(), albedo, metallic);\n"
        "    vec3 ibl = max(uEnvironmentAverage, vec3(0.0)) * max(uEnvironmentIntensity, 0.0) *\n"
        "        albedo * (1.0 - metallic) * ao / PI +\n"
        "        environmentColor(reflected, roughness) * fresnelSchlick(max(dot(normal, view_direction), 0.0), f0) * ao;\n"
        "    if (uClearcoat > 0.0)\n"
        "        ibl += clamp(uClearcoat, 0.0, 1.0) * environmentColor(reflected, max(uClearcoatRoughness, 0.04)) *\n"
        "            fresnelSchlick(max(dot(normal, view_direction), 0.0), vec3(0.04)) * ao;\n"
        "    vec3 emissive = max(uEmissiveColor, vec3(0.0)) * max(uEmissiveStrength, 0.0);\n"
        "    if (uHasEmissiveMap != 0) emissive *= pow(max(texture2D(uEmissiveMap, emissive_uv).rgb, vec3(0.0)), vec3(2.2));\n"
        "    vec3 lit = max(direct + indirect + ambient + ibl + emissive, vec3(0.0));\n"
        "    if (uTransmission > 0.0) {\n"
        "        float eta = 1.0 / max(uIor, 1.0001);\n"
        "        vec3 refracted = refract(-view_direction, normal, eta);\n"
        "        if (dot(refracted, refracted) <= 1.0e-8) refracted = -view_direction;\n"
        "        vec3 transmitted = environmentColor(refracted, roughness);\n"
        "        if (uThickness > 0.0 && uAttenuationDistance > 1.0e-6) {\n"
        "            float optical_distance = uThickness / uAttenuationDistance;\n"
        "            transmitted *= pow(max(uAttenuationColor, vec3(1.0e-4)), vec3(optical_distance));\n"
        "        }\n"
        "        lit = mix(lit, transmitted + emissive, clamp(uTransmission, 0.0, 1.0));\n"
        "    }\n"
        "    if (uDiffuseTransmission > 0.0)\n"
        "        lit += max(uEnvironmentAverage, vec3(0.0)) * max(uEnvironmentIntensity, 0.0) *\n"
        "            max(uDiffuseTransmissionColor, vec3(0.0)) * albedo * clamp(uDiffuseTransmission, 0.0, 1.0) / PI;\n"
        "    vec3 linear_color = uUnlit != 0 ? max(albedo + emissive, vec3(0.0)) : lit;\n"
    );
    replaceOnce(
        source,
        "    vec3 sampled = pow(max(texture2D(uEnvironment, environmentUv(direction)).rgb, vec3(0.0)), vec3(2.2));\n",
        "    vec3 sampled = average;\n"
    );
    return source;
}

inline std::string projectionAwareSkyFragment()
{
    std::string source = horse_pbr_sky_fragment;
    replaceOnce(
        source,
        "    vec3 direction = normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * uTanHalfFov) + uCameraUp * (ndc.y * uTanHalfFov));\n",
        "    float projection_scale = abs(uTanHalfFov);\n"
        "    bool orthographic = uTanHalfFov < 0.0;\n"
        "    vec3 direction = orthographic\n"
        "        ? normalize(uCameraForward)\n"
        "        : normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * projection_scale) +\n"
        "            uCameraUp * (ndc.y * projection_scale));\n"
    );
    replaceOnce(
        source,
        "    float previous_z = max(dot(direction, uPreviousCameraForward), 1.0e-5);\n"
        "    vec2 previous_ndc = vec2(\n"
        "        dot(direction, uPreviousCameraRight) / (previous_z * uAspect * uTanHalfFov),\n"
        "        dot(direction, uPreviousCameraUp) / (previous_z * uTanHalfFov)\n"
        "    );\n"
        "    vec2 velocity = (ndc - previous_ndc) * 0.5;",
        "    vec2 velocity = vec2(0.0);\n"
        "    if (!orthographic) {\n"
        "        float previous_z = max(dot(direction, uPreviousCameraForward), 1.0e-5);\n"
        "        vec2 previous_ndc = vec2(\n"
        "            dot(direction, uPreviousCameraRight) / (previous_z * uAspect * projection_scale),\n"
        "            dot(direction, uPreviousCameraUp) / (previous_z * projection_scale)\n"
        "        );\n"
        "        velocity = (ndc - previous_ndc) * 0.5;\n"
        "    }"
    );
    return source;
}

inline std::string textureAwareShadowFragment()
{
    std::string source = horse_pbr_shadow_fragment;
    replaceOnce(
        source,
        "uniform float uAlphaCutoff;\n",
        "uniform float uAlphaCutoff;\n"
        "uniform vec4 uBaseUvTransform;\n"
        "uniform float uBaseUvRotation;\n"
    );
    replaceOnce(
        source,
        "void main()\n{\n"
        "    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, vUv) : vec4(1.0);\n",
        "vec2 transformedUv(vec2 uv)\n"
        "{\n"
        "    vec2 scaled = uv * uBaseUvTransform.zw;\n"
        "    float c = cos(uBaseUvRotation), s = sin(uBaseUvRotation);\n"
        "    return uBaseUvTransform.xy + vec2(c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y);\n"
        "}\n"
        "void main()\n{\n"
        "    vec4 texel = uHasTexture != 0 ? texture2D(uDiffuse, transformedUv(vUv)) : vec4(1.0);\n"
    );
    return source;
}

} // namespace

inline const std::string main_fragment_storage = compatibleMainFragment();
inline const char *main_fragment = main_fragment_storage.c_str();
inline const std::string sky_fragment_storage = projectionAwareSkyFragment();
inline const char *sky_fragment = sky_fragment_storage.c_str();
inline const std::string shadow_fragment_storage = textureAwareShadowFragment();
inline const char *shadow_fragment = shadow_fragment_storage.c_str();

} // namespace Renderer::RasterizerShaders

#endif
