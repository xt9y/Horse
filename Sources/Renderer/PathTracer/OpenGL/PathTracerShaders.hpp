#ifndef RW_ENGINE_RENDERER_PATHTRACER_OPENGL_SHADERS_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_OPENGL_SHADERS_HPP

#include <string>

#define trace horse_pbr_trace
#include "Renderer/PathTracer/OpenGL/PbrTraceShaders.hpp"
#undef trace
#include "Renderer/PathTracer/OpenGL/PathTracerPresentShaders.hpp"

namespace Renderer::PathTracerShaders {
namespace {

inline void replaceAll(std::string& source, const char *from, const char *to)
{
    const std::size_t from_size = std::char_traits<char>::length(from);
    const std::size_t to_size = std::char_traits<char>::length(to);
    std::size_t position = 0u;
    while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from_size, to);
        position += to_size;
    }
}

inline std::string projectionAwareTrace()
{
    std::string source = horse_pbr_trace;
    replaceAll(
        source,
        "    vec3 depth_direction = normalize(\n"
        "        uCameraForward + uCameraRight * (depth_ndc.x * uAspect * uTanHalfFov) +\n"
        "        uCameraUp * (depth_ndc.y * uTanHalfFov));\n"
        "    Hit depth_hit = traceClosest(uCameraPosition, depth_direction, INF, true);\n"
        "    if (!depth_hit.found) return INF;\n"
        "    return max(dot(depth_hit.position - uCameraPosition, normalize(uCameraForward)), RAY_EPSILON);",
        "    float projection_scale = abs(uTanHalfFov);\n"
        "    bool orthographic = uTanHalfFov < 0.0;\n"
        "    vec3 depth_origin = orthographic\n"
        "        ? uCameraPosition + uCameraRight * (depth_ndc.x * uAspect * projection_scale) +\n"
        "            uCameraUp * (depth_ndc.y * projection_scale)\n"
        "        : uCameraPosition;\n"
        "    vec3 depth_direction = orthographic\n"
        "        ? normalize(uCameraForward)\n"
        "        : normalize(uCameraForward + uCameraRight * (depth_ndc.x * uAspect * projection_scale) +\n"
        "            uCameraUp * (depth_ndc.y * projection_scale));\n"
        "    Hit depth_hit = traceClosest(depth_origin, depth_direction, INF, true);\n"
        "    if (!depth_hit.found) return INF;\n"
        "    return max(dot(depth_hit.position - depth_origin, normalize(uCameraForward)), RAY_EPSILON);"
    );
    replaceAll(
        source,
        "        vec3 direction = normalize(uCameraForward +\n"
        "            uCameraRight * (ndc.x * uAspect * uTanHalfFov) + uCameraUp * (ndc.y * uTanHalfFov));\n"
        "        sample_radiance += tracePath(uCameraPosition, direction);",
        "        float projection_scale = abs(uTanHalfFov);\n"
        "        bool orthographic = uTanHalfFov < 0.0;\n"
        "        vec3 ray_origin = orthographic\n"
        "            ? uCameraPosition + uCameraRight * (ndc.x * uAspect * projection_scale) +\n"
        "                uCameraUp * (ndc.y * projection_scale)\n"
        "            : uCameraPosition;\n"
        "        vec3 direction = orthographic\n"
        "            ? normalize(uCameraForward)\n"
        "            : normalize(uCameraForward + uCameraRight * (ndc.x * uAspect * projection_scale) +\n"
        "                uCameraUp * (ndc.y * projection_scale));\n"
        "        sample_radiance += tracePath(ray_origin, direction);"
    );
    return source;
}

inline std::string advancedMaterialTrace()
{
    std::string source = projectionAwareTrace();
    replaceAll(
        source,
        "struct Material { vec4 base_color; ivec4 data; };\n",
        "struct Material { vec4 base_color; ivec4 data; };\n"
        "struct AdvancedMaterial {\n"
        "    vec4 emissive_strength; vec4 pbr; vec4 specular; vec4 clearcoat_sheen;\n"
        "    vec4 sheen_thickness; vec4 attenuation; vec4 diffuse_transmission;\n"
        "    vec4 anisotropy_iridescence; vec4 iridescence_dispersion_ior; vec4 misc;\n"
        "    ivec4 tex0; ivec4 tex1; ivec4 tex2; ivec4 tex3; ivec4 tex4; ivec4 tex5;\n"
        "};\n"
    );
    replaceAll(
        source,
        "layout(std430, binding = 6) readonly buffer EntityVisibility { uint entity_visibility[]; };\n",
        "layout(std430, binding = 6) readonly buffer EntityVisibility { uint entity_visibility[]; };\n"
        "layout(std430, binding = 7) readonly buffer AdvancedMaterials { AdvancedMaterial advanced_materials[]; };\n"
    );
    replaceAll(
        source,
        "    float clearcoat;\n};\n",
        "    float clearcoat;\n"
        "    float clearcoat_roughness; vec3 sheen_color; float sheen_roughness;\n"
        "    float transmission; float thickness; vec3 attenuation_color; float attenuation_distance;\n"
        "    float specular_factor; vec3 specular_color; float ior; float dispersion;\n"
        "    float anisotropy_strength; float anisotropy_rotation; float iridescence; float iridescence_ior;\n"
        "    float iridescence_thickness; float diffuse_transmission; vec3 diffuse_transmission_color;\n"
        "    float alpha; int alpha_mode; bool unlit; bool double_sided;\n};\n"
    );
    replaceAll(
        source,
        "bool alphaCutoutPass(uint material_index, vec2 uv)\n{\n"
        "    if (material_index >= uint(max(uMaterialCount, 0))) return true;\n"
        "    Material material = materials[material_index];\n"
        "    float alpha = clamp(material.base_color.a, 0.0, 1.0);\n"
        "    if (material.data.x >= 0 && material.data.x < 16)\n"
        "        alpha *= sampleTextureSlot(material.data.x, uv).a;\n"
        "    int opacity_slot = packedTextureSlot(material, 5);\n"
        "    if (opacity_slot >= 0 && opacity_slot < 16)\n"
        "        alpha *= sampleTextureSlot(opacity_slot, uv).r;\n"
        "    return alpha >= uAlphaCutoff;\n}\n",
        "bool alphaCutoutPass(uint material_index, vec2 uv)\n{\n"
        "    if (material_index >= uint(max(uMaterialCount, 0))) return true;\n"
        "    Material material = materials[material_index];\n"
        "    AdvancedMaterial advanced = advanced_materials[material_index];\n"
        "    int mode = int(advanced.misc.w + 0.5);\n"
        "    if (mode == 0) return true;\n"
        "    float alpha = clamp(material.base_color.a, 0.0, 1.0);\n"
        "    if (advanced.tex0.x >= 0 && advanced.tex0.x < 16) alpha *= sampleTextureSlot(advanced.tex0.x, uv).a;\n"
        "    if (advanced.tex1.z >= 0 && advanced.tex1.z < 16) alpha *= sampleTextureSlot(advanced.tex1.z, uv).r;\n"
        "    float cutoff = mode == 1 ? clamp(advanced.misc.x, 0.0, 1.0) : (1.0 / 255.0);\n"
        "    return alpha >= cutoff;\n}\n"
    );
    replaceAll(
        source,
        "vec3 tangentNormal(Hit hit, Material material)\n{\n"
        "    int slot = packedTextureSlot(material, 0);",
        "vec3 tangentNormal(Hit hit, Material material)\n{\n"
        "    AdvancedMaterial advanced = advanced_materials[hit.material];\n"
        "    int slot = advanced.tex0.y;"
    );
    replaceAll(
        source,
        "    vec3 mapped = sampleTextureSlot(slot, hit.uv).xyz * 2.0 - 1.0;\n"
        "    return dot(mapped, mapped) <= 1.0e-10 ? n : normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);",
        "    vec3 mapped = sampleTextureSlot(slot, hit.uv).xyz * 2.0 - 1.0;\n"
        "    mapped.xy *= max(advanced.pbr.w, 0.0);\n"
        "    return dot(mapped, mapped) <= 1.0e-10 ? n : normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);"
    );
    replaceAll(
        source,
        "Surface surfaceAt(Hit hit)\n{\n"
        "    Surface surface;\n"
        "    Material material = hit.material < uint(max(uMaterialCount, 0)) ? materials[hit.material] : materials[0];\n"
        "    surface.albedo = max(material.base_color.rgb, vec3(0.0));\n"
        "    if (material.data.x >= 0 && material.data.x < 16)\n"
        "        surface.albedo *= pow(max(sampleTextureSlot(material.data.x, hit.uv).rgb, vec3(0.0)), vec3(2.2));\n"
        "    surface.roughness = max(packedParameter(material, 0), 0.04);\n"
        "    surface.metallic = packedParameter(material, 1);\n"
        "    surface.ao = packedParameter(material, 2);\n"
        "    surface.clearcoat = packedParameter(material, 3);\n"
        "    int roughness_slot = packedTextureSlot(material, 1);\n"
        "    int metallic_slot = packedTextureSlot(material, 2);\n"
        "    int ao_slot = packedTextureSlot(material, 3);\n"
        "    int emissive_slot = packedTextureSlot(material, 4);\n"
        "    if (roughness_slot >= 0 && roughness_slot < 16)\n"
        "        surface.roughness = max(surface.roughness * sampleTextureSlot(roughness_slot, hit.uv).r, 0.04);\n"
        "    if (metallic_slot >= 0 && metallic_slot < 16)\n"
        "        surface.metallic *= sampleTextureSlot(metallic_slot, hit.uv).r;\n"
        "    if (ao_slot >= 0 && ao_slot < 16)\n"
        "        surface.ao *= sampleTextureSlot(ao_slot, hit.uv).r;\n"
        "    surface.metallic = clamp(surface.metallic, 0.0, 1.0);\n"
        "    surface.ao = clamp(surface.ao, 0.0, 1.0);\n"
        "    surface.normal = tangentNormal(hit, material);\n"
        "    surface.emissive = packedEmissiveColor(material) * packedEmissiveStrength(material);\n"
        "    if (emissive_slot >= 0 && emissive_slot < 16)\n"
        "        surface.emissive *= pow(max(sampleTextureSlot(emissive_slot, hit.uv).rgb, vec3(0.0)), vec3(2.2));\n"
        "    return surface;\n}\n",
        "Surface surfaceAt(Hit hit)\n{\n"
        "    Surface surface;\n"
        "    uint index = hit.material < uint(max(uMaterialCount, 0)) ? hit.material : 0u;\n"
        "    Material material = materials[index]; AdvancedMaterial a = advanced_materials[index];\n"
        "    surface.albedo = max(material.base_color.rgb, vec3(0.0));\n"
        "    if (a.tex0.x >= 0 && a.tex0.x < 16) surface.albedo *= pow(max(sampleTextureSlot(a.tex0.x, hit.uv).rgb, vec3(0.0)), vec3(2.2));\n"
        "    surface.roughness = max(a.pbr.x, 0.04); surface.metallic = clamp(a.pbr.y, 0.0, 1.0); surface.ao = clamp(a.pbr.z, 0.0, 1.0);\n"
        "    if (a.tex0.z >= 0 && a.tex0.z < 16) surface.roughness = max(surface.roughness * sampleTextureSlot(a.tex0.z, hit.uv).r, 0.04);\n"
        "    if (a.tex0.w >= 0 && a.tex0.w < 16) surface.metallic *= sampleTextureSlot(a.tex0.w, hit.uv).r;\n"
        "    if (a.tex1.x >= 0 && a.tex1.x < 16) surface.ao *= sampleTextureSlot(a.tex1.x, hit.uv).r;\n"
        "    surface.normal = tangentNormal(hit, material);\n"
        "    surface.emissive = max(a.emissive_strength.rgb, vec3(0.0)) * max(a.emissive_strength.a, 0.0);\n"
        "    if (a.tex1.y >= 0 && a.tex1.y < 16) surface.emissive *= pow(max(sampleTextureSlot(a.tex1.y, hit.uv).rgb, vec3(0.0)), vec3(2.2));\n"
        "    surface.clearcoat = clamp(a.clearcoat_sheen.x * (a.tex1.w >= 0 && a.tex1.w < 16 ? sampleTextureSlot(a.tex1.w, hit.uv).r : 1.0), 0.0, 1.0);\n"
        "    surface.clearcoat_roughness = clamp(a.clearcoat_sheen.y * (a.tex2.x >= 0 && a.tex2.x < 16 ? sampleTextureSlot(a.tex2.x, hit.uv).r : 1.0), 0.0, 1.0);\n"
        "    surface.sheen_color = max(a.sheen_thickness.rgb, vec3(0.0)) * (a.tex2.z >= 0 && a.tex2.z < 16 ? pow(max(sampleTextureSlot(a.tex2.z, hit.uv).rgb, vec3(0.0)), vec3(2.2)) : vec3(1.0));\n"
        "    surface.sheen_roughness = clamp(a.clearcoat_sheen.z * (a.tex2.w >= 0 && a.tex2.w < 16 ? sampleTextureSlot(a.tex2.w, hit.uv).a : 1.0), 0.0, 1.0);\n"
        "    surface.transmission = clamp(a.clearcoat_sheen.w * (a.tex3.x >= 0 && a.tex3.x < 16 ? sampleTextureSlot(a.tex3.x, hit.uv).r : 1.0), 0.0, 1.0);\n"
        "    surface.thickness = max(a.sheen_thickness.w * (a.tex3.y >= 0 && a.tex3.y < 16 ? sampleTextureSlot(a.tex3.y, hit.uv).g : 1.0), 0.0);\n"
        "    surface.attenuation_color = max(a.attenuation.rgb, vec3(1.0e-4)); surface.attenuation_distance = max(a.attenuation.w, 0.0);\n"
        "    surface.specular_factor = max(a.specular.x * (a.tex3.z >= 0 && a.tex3.z < 16 ? sampleTextureSlot(a.tex3.z, hit.uv).a : 1.0), 0.0);\n"
        "    surface.specular_color = max(a.specular.yzw, vec3(0.0)) * (a.tex3.w >= 0 && a.tex3.w < 16 ? pow(max(sampleTextureSlot(a.tex3.w, hit.uv).rgb, vec3(0.0)), vec3(2.2)) : vec3(1.0));\n"
        "    surface.ior = max(a.iridescence_dispersion_ior.w, 1.0001); surface.dispersion = max(a.iridescence_dispersion_ior.z, 0.0);\n"
        "    surface.anisotropy_strength = clamp(a.anisotropy_iridescence.x * (a.tex4.z >= 0 && a.tex4.z < 16 ? sampleTextureSlot(a.tex4.z, hit.uv).b : 1.0), 0.0, 1.0);\n"
        "    surface.anisotropy_rotation = a.anisotropy_iridescence.y;\n"
        "    surface.iridescence = clamp(a.anisotropy_iridescence.z * (a.tex4.x >= 0 && a.tex4.x < 16 ? sampleTextureSlot(a.tex4.x, hit.uv).r : 1.0), 0.0, 1.0);\n"
        "    surface.iridescence_ior = max(a.anisotropy_iridescence.w, 1.0);\n"
        "    float iri_t = a.tex4.y >= 0 && a.tex4.y < 16 ? sampleTextureSlot(a.tex4.y, hit.uv).g : 1.0;\n"
        "    surface.iridescence_thickness = mix(a.iridescence_dispersion_ior.x, a.iridescence_dispersion_ior.y, iri_t);\n"
        "    surface.diffuse_transmission = clamp(a.diffuse_transmission.w * (a.tex4.w >= 0 && a.tex4.w < 16 ? sampleTextureSlot(a.tex4.w, hit.uv).a : 1.0), 0.0, 1.0);\n"
        "    surface.diffuse_transmission_color = max(a.diffuse_transmission.rgb, vec3(0.0)) * (a.tex5.x >= 0 && a.tex5.x < 16 ? pow(max(sampleTextureSlot(a.tex5.x, hit.uv).rgb, vec3(0.0)), vec3(2.2)) : vec3(1.0));\n"
        "    surface.alpha = clamp(material.base_color.a, 0.0, 1.0); if (a.tex0.x >= 0 && a.tex0.x < 16) surface.alpha *= sampleTextureSlot(a.tex0.x, hit.uv).a; if (a.tex1.z >= 0 && a.tex1.z < 16) surface.alpha *= sampleTextureSlot(a.tex1.z, hit.uv).r;\n"
        "    surface.alpha_mode = int(a.misc.w + 0.5); surface.unlit = a.misc.y > 0.5; surface.double_sided = a.misc.z > 0.5;\n"
        "    return surface;\n}\n"
    );
    replaceAll(
        source,
        "vec3 directBrdf(Surface surface, vec3 view_direction, vec3 light_direction, vec3 incoming)\n{",
        "vec3 dielectricF0(Surface surface) { float f = (surface.ior - 1.0) / (surface.ior + 1.0); return surface.specular_color * surface.specular_factor * (f * f); }\n"
        "vec3 iridescentF0(Surface s, vec3 f0, float cosine) { if (s.iridescence <= 0.0) return f0; float phase=s.iridescence_ior*s.iridescence_thickness*max(cosine,0.0)*0.0125663706; vec3 film=0.5+0.5*cos(vec3(phase,phase*1.29+2.0944,phase*1.61+4.18879)); return mix(f0,film,s.iridescence); }\n"
        "float anisotropicDistribution(Surface s, vec3 n, vec3 h) { if(s.anisotropy_strength<=1.0e-5) return distributionGGX(n,h,s.roughness); vec3 t=normalize(abs(n.y)<0.999?cross(vec3(0,1,0),n):cross(vec3(1,0,0),n)); vec3 b=normalize(cross(n,t)); float c=cos(s.anisotropy_rotation),q=sin(s.anisotropy_rotation); vec3 tr=t*c+b*q, br=-t*q+b*c; float a=max(s.roughness*s.roughness,0.0025), aspect=sqrt(max(1.0-0.9*s.anisotropy_strength,0.1)); float ax=max(a/aspect,0.0025), ay=max(a*aspect,0.0025); float hx=dot(h,tr)/ax,hy=dot(h,br)/ay,hz=max(dot(h,n),0.0),d=hx*hx+hy*hy+hz*hz; return 1.0/max(PI*ax*ay*d*d,1.0e-6); }\n"
        "vec3 directBrdf(Surface surface, vec3 view_direction, vec3 light_direction, vec3 incoming)\n{"
    );
    replaceAll(source, "    vec3 f0 = mix(vec3(0.04), surface.albedo, surface.metallic);\n", "    vec3 f0 = mix(iridescentF0(surface, dielectricF0(surface), max(dot(n, v), 0.0)), surface.albedo, surface.metallic);\n");
    replaceAll(source, "    float d = distributionGGX(n, h, surface.roughness);\n", "    float d = anisotropicDistribution(surface, n, h);\n");
    replaceAll(source, "        float coat_d = distributionGGX(n, h, max(surface.roughness * 0.35, 0.04));\n        float coat_g = geometrySmith(n, v, l, 0.25);\n", "        float coat_roughness = max(surface.clearcoat_roughness, 0.04);\n        float coat_d = distributionGGX(n, h, coat_roughness);\n        float coat_g = geometrySmith(n, v, l, coat_roughness);\n");
    replaceAll(source, "    return (diffuse + specular) * incoming * nl;\n", "    float sheen_grazing = pow(clamp(1.0 - max(dot(n,h),0.0),0.0,1.0), 1.0 + 4.0 * surface.sheen_roughness);\n    vec3 sheen = surface.sheen_color * sheen_grazing;\n    return (diffuse + specular + sheen) * incoming * nl;\n");
    replaceAll(
        source,
        "    Surface surface = surfaceAt(hit);\n    vec3 radiance = surface.emissive;\n",
        "    Surface surface = surfaceAt(hit);\n    if (surface.unlit) return applyEnvironmentFog(max(surface.albedo + surface.emissive, vec3(0.0)), hit.distance);\n    vec3 radiance = surface.emissive;\n"
    );
    replaceAll(
        source,
        "    vec3 f0 = mix(vec3(0.04), surface.albedo, surface.metallic);\n"
        "    vec3 f = fresnelSchlick(max(dot(surface.normal, view_direction), 0.0), f0);\n",
        "    vec3 f0 = mix(iridescentF0(surface, dielectricF0(surface), max(dot(surface.normal, view_direction), 0.0)), surface.albedo, surface.metallic);\n"
        "    vec3 f = fresnelSchlick(max(dot(surface.normal, view_direction), 0.0), f0);\n"
    );
    replaceAll(
        source,
        "    radiance += env_diffuse + env_specular;\n\n    return applyEnvironmentFog(max(radiance, vec3(0.0)), hit.distance);",
        "    radiance += env_diffuse + env_specular;\n"
        "    if (surface.transmission > 0.0) {\n"
        "        float eta_g = 1.0 / surface.ior; vec3 refracted_g = refract(-view_direction, surface.normal, eta_g); if(dot(refracted_g,refracted_g)<=1.0e-8) refracted_g=-view_direction;\n"
        "        vec3 transmitted = environmentRadiance(refracted_g, surface.roughness);\n"
        "        float dispersion = surface.dispersion * 0.01; if(dispersion>0.0){ vec3 rr=refract(-view_direction,surface.normal,1.0/max(surface.ior+dispersion,1.0001)); vec3 rb=refract(-view_direction,surface.normal,1.0/max(surface.ior-dispersion,1.0001)); if(dot(rr,rr)<=1.0e-8)rr=refracted_g; if(dot(rb,rb)<=1.0e-8)rb=refracted_g; transmitted=vec3(environmentRadiance(rr,surface.roughness).r,transmitted.g,environmentRadiance(rb,surface.roughness).b);}\n"
        "        if(surface.thickness>0.0 && surface.attenuation_distance>1.0e-6) transmitted *= pow(surface.attenuation_color, vec3(surface.thickness/surface.attenuation_distance));\n"
        "        radiance = mix(radiance, transmitted + surface.emissive, surface.transmission);\n"
        "    }\n"
        "    if(surface.diffuse_transmission>0.0) radiance += max(gi_data[3].rgb,vec3(0.0))*max(gi_data[3].a,0.0)*surface.diffuse_transmission_color*surface.albedo*surface.diffuse_transmission/PI;\n"
        "    return applyEnvironmentFog(max(radiance, vec3(0.0)), hit.distance);"
    );
    return source;
}

} // namespace

inline const std::string trace_storage = advancedMaterialTrace();
inline const char *trace = trace_storage.c_str();

} // namespace Renderer::PathTracerShaders

#endif
