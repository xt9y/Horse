#ifndef HORSE_RENDERER_TRACE_METAL_ADVANCED_MATERIAL_SHADERS_HPP
#define HORSE_RENDERER_TRACE_METAL_ADVANCED_MATERIAL_SHADERS_HPP

#include <string>

namespace Renderer::Trace::Metal {
namespace {

inline void replaceAdvancedSource(std::string& source, const char *from, const char *to)
{
    const std::size_t from_size = std::char_traits<char>::length(from);
    const std::size_t to_size = std::char_traits<char>::length(to);
    std::size_t position = 0u;
    while ((position = source.find(from, position)) != std::string::npos) {
        source.replace(position, from_size, to);
        position += to_size;
    }
}

} // namespace

inline std::string advancedMaterialShaderSource(std::string source)
{
    replaceAdvancedSource(
        source,
        "struct Material { float4 base_color; int4 data; };\n",
        "struct Material { float4 base_color; int4 data; };\n"
        "struct AdvancedMaterial {\n"
        "    float4 emissive_strength; float4 pbr; float4 specular; float4 clearcoat_sheen;\n"
        "    float4 sheen_thickness; float4 attenuation; float4 diffuse_transmission;\n"
        "    float4 anisotropy_iridescence; float4 iridescence_dispersion_ior; float4 misc;\n"
        "    int4 tex0; int4 tex1; int4 tex2; int4 tex3; int4 tex4; int4 tex5;\n"
        "};\n"
    );
    replaceAdvancedSource(
        source,
        "    float clearcoat;\n};\n",
        "    float clearcoat;\n"
        "    float clearcoat_roughness; float3 sheen_color; float sheen_roughness;\n"
        "    float transmission; float thickness; float3 attenuation_color; float attenuation_distance;\n"
        "    float specular_factor; float3 specular_color; float ior; float dispersion;\n"
        "    float anisotropy_strength; float anisotropy_rotation; float iridescence; float iridescence_ior;\n"
        "    float iridescence_thickness; float diffuse_transmission; float3 diffuse_transmission_color;\n"
        "    float alpha; int alpha_mode; bool unlit; bool double_sided;\n};\n"
    );

    // Thread the advanced material buffer through the existing trace functions.
    replaceAdvancedSource(
        source,
        "device const Material *materials,\n    device const uint *entity_visibility,",
        "device const Material *materials,\n    device const AdvancedMaterial *advanced_materials,\n    device const uint *entity_visibility,"
    );
    replaceAdvancedSource(
        source,
        "device const Material *materials,\n    constant TraceUniforms& uniforms,",
        "device const Material *materials,\n    device const AdvancedMaterial *advanced_materials,\n    constant TraceUniforms& uniforms,"
    );
    replaceAdvancedSource(
        source,
        "device const Material *materials,\n    device const float4 *gi_data,",
        "device const Material *materials,\n    device const AdvancedMaterial *advanced_materials,\n    device const float4 *gi_data,"
    );
    replaceAdvancedSource(
        source,
        "device const Material *materials [[buffer(2)]],\n    constant TraceUniforms& uniforms [[buffer(3)]],",
        "device const Material *materials [[buffer(2)]],\n    constant TraceUniforms& uniforms [[buffer(3)]],"
    );
    replaceAdvancedSource(
        source,
        "device const uint *entity_visibility [[buffer(6)]],\n",
        "device const uint *entity_visibility [[buffer(6)]],\n    device const AdvancedMaterial *advanced_materials [[buffer(7)]],\n"
    );
    replaceAdvancedSource(source, "materials, entity_visibility", "materials, advanced_materials, entity_visibility");
    replaceAdvancedSource(source, "materials, uniforms", "materials, advanced_materials, uniforms");
    replaceAdvancedSource(source, "materials, gi_data", "materials, advanced_materials, gi_data");

    replaceAdvancedSource(
        source,
        "    Material material = materials[material_index];\n"
        "    float alpha = clamp(material.base_color.a, 0.0f, 1.0f);\n"
        "    if (material.data.x >= 0 && material.data.x < 32)\n"
        "        alpha *= sampleTextureSlot(material.data.x, uv, textures, material_sampler).a;\n"
        "    int opacity_slot = packedTextureSlot(material, 5);\n"
        "    if (opacity_slot >= 0 && opacity_slot < 32)\n"
        "        alpha *= sampleTextureSlot(opacity_slot, uv, textures, material_sampler).r;\n"
        "    return alpha >= uniforms.resolution_aspect.w;",
        "    Material material = materials[material_index];\n"
        "    AdvancedMaterial advanced = advanced_materials[material_index];\n"
        "    int alpha_mode = int(advanced.misc.w + 0.5f);\n"
        "    if (alpha_mode == 0) return true;\n"
        "    float alpha = clamp(material.base_color.a, 0.0f, 1.0f);\n"
        "    if (advanced.tex0.x >= 0 && advanced.tex0.x < 32)\n"
        "        alpha *= sampleTextureSlot(advanced.tex0.x, uv, textures, material_sampler).a;\n"
        "    if (advanced.tex1.z >= 0 && advanced.tex1.z < 32)\n"
        "        alpha *= sampleTextureSlot(advanced.tex1.z, uv, textures, material_sampler).r;\n"
        "    float cutoff = alpha_mode == 1 ? clamp(advanced.misc.x, 0.0f, 1.0f) : (1.0f / 255.0f);\n"
        "    return alpha >= cutoff;"
    );

    replaceAdvancedSource(
        source,
        "    Material material,\n    device const Triangle *triangles,",
        "    Material material,\n    AdvancedMaterial advanced,\n    device const Triangle *triangles,"
    );
    replaceAdvancedSource(source, "    int slot = packedTextureSlot(material, 0);", "    int slot = advanced.tex0.y;");
    replaceAdvancedSource(
        source,
        "    float3 mapped = sampleTextureSlot(slot, hit.uv, textures, material_sampler).xyz * 2.0f - 1.0f;\n"
        "    return dot(mapped, mapped) <= 1.0e-10f ? n : normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);",
        "    float3 mapped = sampleTextureSlot(slot, hit.uv, textures, material_sampler).xyz * 2.0f - 1.0f;\n"
        "    mapped.xy *= max(advanced.pbr.w, 0.0f);\n"
        "    return dot(mapped, mapped) <= 1.0e-10f ? n : normalize(tangent * mapped.x + bitangent * mapped.y + n * mapped.z);"
    );

    const char *surface_begin =
        "    Surface surface;\n"
        "    Material material = hit.material < uint(max(uniforms.counts.z, 0)) ? materials[hit.material] : materials[0];\n"
        "    surface.albedo = max(material.base_color.rgb, float3(0.0f));\n"
        "    if (material.data.x >= 0 && material.data.x < 32)\n"
        "        surface.albedo *= pow(max(sampleTextureSlot(material.data.x, hit.uv, textures, material_sampler).rgb,\n"
        "            float3(0.0f)), float3(2.2f));\n"
        "    surface.roughness = max(packedParameter(material, 0), 0.04f);\n"
        "    surface.metallic = packedParameter(material, 1);\n"
        "    surface.ao = packedParameter(material, 2);\n"
        "    surface.clearcoat = packedParameter(material, 3);\n"
        "    int roughness_slot = packedTextureSlot(material, 1);\n"
        "    int metallic_slot = packedTextureSlot(material, 2);\n"
        "    int ao_slot = packedTextureSlot(material, 3);\n"
        "    int emissive_slot = packedTextureSlot(material, 4);\n"
        "    if (roughness_slot >= 0 && roughness_slot < 32)\n"
        "        surface.roughness = max(surface.roughness * sampleTextureSlot(roughness_slot, hit.uv, textures, material_sampler).r, 0.04f);\n"
        "    if (metallic_slot >= 0 && metallic_slot < 32)\n"
        "        surface.metallic *= sampleTextureSlot(metallic_slot, hit.uv, textures, material_sampler).r;\n"
        "    if (ao_slot >= 0 && ao_slot < 32)\n"
        "        surface.ao *= sampleTextureSlot(ao_slot, hit.uv, textures, material_sampler).r;\n"
        "    surface.metallic = clamp(surface.metallic, 0.0f, 1.0f);\n"
        "    surface.ao = clamp(surface.ao, 0.0f, 1.0f);\n"
        "    surface.normal = tangentNormal(hit, material, triangles, textures, material_sampler);\n"
        "    surface.emissive = packedEmissiveColor(material) * packedEmissiveStrength(material);\n"
        "    if (emissive_slot >= 0 && emissive_slot < 32)\n"
        "        surface.emissive *= pow(max(sampleTextureSlot(emissive_slot, hit.uv, textures, material_sampler).rgb,\n"
        "            float3(0.0f)), float3(2.2f));\n"
        "    return surface;";

    const char *surface_advanced =
        "    Surface surface;\n"
        "    uint index = hit.material < uint(max(uniforms.counts.z, 0)) ? hit.material : 0u;\n"
        "    Material material = materials[index];\n"
        "    AdvancedMaterial a = advanced_materials[index];\n"
        "    surface.albedo = max(material.base_color.rgb, float3(0.0f));\n"
        "    if (a.tex0.x >= 0 && a.tex0.x < 32) surface.albedo *= pow(max(sampleTextureSlot(a.tex0.x, hit.uv, textures, material_sampler).rgb, float3(0.0f)), float3(2.2f));\n"
        "    surface.roughness = max(a.pbr.x, 0.04f); surface.metallic = clamp(a.pbr.y, 0.0f, 1.0f); surface.ao = clamp(a.pbr.z, 0.0f, 1.0f);\n"
        "    if (a.tex0.z >= 0 && a.tex0.z < 32) surface.roughness = max(surface.roughness * sampleTextureSlot(a.tex0.z, hit.uv, textures, material_sampler).r, 0.04f);\n"
        "    if (a.tex0.w >= 0 && a.tex0.w < 32) surface.metallic *= sampleTextureSlot(a.tex0.w, hit.uv, textures, material_sampler).r;\n"
        "    if (a.tex1.x >= 0 && a.tex1.x < 32) surface.ao *= sampleTextureSlot(a.tex1.x, hit.uv, textures, material_sampler).r;\n"
        "    surface.normal = tangentNormal(hit, material, a, triangles, textures, material_sampler);\n"
        "    surface.emissive = max(a.emissive_strength.rgb, float3(0.0f)) * max(a.emissive_strength.a, 0.0f);\n"
        "    if (a.tex1.y >= 0 && a.tex1.y < 32) surface.emissive *= pow(max(sampleTextureSlot(a.tex1.y, hit.uv, textures, material_sampler).rgb, float3(0.0f)), float3(2.2f));\n"
        "    surface.clearcoat = clamp(a.clearcoat_sheen.x * (a.tex1.w >= 0 && a.tex1.w < 32 ? sampleTextureSlot(a.tex1.w, hit.uv, textures, material_sampler).r : 1.0f), 0.0f, 1.0f);\n"
        "    surface.clearcoat_roughness = clamp(a.clearcoat_sheen.y * (a.tex2.x >= 0 && a.tex2.x < 32 ? sampleTextureSlot(a.tex2.x, hit.uv, textures, material_sampler).r : 1.0f), 0.0f, 1.0f);\n"
        "    surface.sheen_color = max(a.sheen_thickness.rgb, float3(0.0f)) * (a.tex2.z >= 0 && a.tex2.z < 32 ? pow(max(sampleTextureSlot(a.tex2.z, hit.uv, textures, material_sampler).rgb, float3(0.0f)), float3(2.2f)) : float3(1.0f));\n"
        "    surface.sheen_roughness = clamp(a.clearcoat_sheen.z * (a.tex2.w >= 0 && a.tex2.w < 32 ? sampleTextureSlot(a.tex2.w, hit.uv, textures, material_sampler).a : 1.0f), 0.0f, 1.0f);\n"
        "    surface.transmission = clamp(a.clearcoat_sheen.w * (a.tex3.x >= 0 && a.tex3.x < 32 ? sampleTextureSlot(a.tex3.x, hit.uv, textures, material_sampler).r : 1.0f), 0.0f, 1.0f);\n"
        "    surface.thickness = max(a.sheen_thickness.w * (a.tex3.y >= 0 && a.tex3.y < 32 ? sampleTextureSlot(a.tex3.y, hit.uv, textures, material_sampler).g : 1.0f), 0.0f);\n"
        "    surface.attenuation_color = max(a.attenuation.rgb, float3(1.0e-4f)); surface.attenuation_distance = max(a.attenuation.w, 0.0f);\n"
        "    surface.specular_factor = max(a.specular.x * (a.tex3.z >= 0 && a.tex3.z < 32 ? sampleTextureSlot(a.tex3.z, hit.uv, textures, material_sampler).a : 1.0f), 0.0f);\n"
        "    surface.specular_color = max(a.specular.yzw, float3(0.0f)) * (a.tex3.w >= 0 && a.tex3.w < 32 ? pow(max(sampleTextureSlot(a.tex3.w, hit.uv, textures, material_sampler).rgb, float3(0.0f)), float3(2.2f)) : float3(1.0f));\n"
        "    surface.ior = max(a.iridescence_dispersion_ior.w, 1.0001f); surface.dispersion = max(a.iridescence_dispersion_ior.z, 0.0f);\n"
        "    surface.anisotropy_strength = clamp(a.anisotropy_iridescence.x * (a.tex4.z >= 0 && a.tex4.z < 32 ? sampleTextureSlot(a.tex4.z, hit.uv, textures, material_sampler).b : 1.0f), 0.0f, 1.0f);\n"
        "    surface.anisotropy_rotation = a.anisotropy_iridescence.y;\n"
        "    surface.iridescence = clamp(a.anisotropy_iridescence.z * (a.tex4.x >= 0 && a.tex4.x < 32 ? sampleTextureSlot(a.tex4.x, hit.uv, textures, material_sampler).r : 1.0f), 0.0f, 1.0f);\n"
        "    surface.iridescence_ior = max(a.anisotropy_iridescence.w, 1.0f);\n"
        "    float iri_t = a.tex4.y >= 0 && a.tex4.y < 32 ? sampleTextureSlot(a.tex4.y, hit.uv, textures, material_sampler).g : 1.0f;\n"
        "    surface.iridescence_thickness = mix(a.iridescence_dispersion_ior.x, a.iridescence_dispersion_ior.y, iri_t);\n"
        "    surface.diffuse_transmission = clamp(a.diffuse_transmission.w * (a.tex4.w >= 0 && a.tex4.w < 32 ? sampleTextureSlot(a.tex4.w, hit.uv, textures, material_sampler).a : 1.0f), 0.0f, 1.0f);\n"
        "    surface.diffuse_transmission_color = max(a.diffuse_transmission.rgb, float3(0.0f)) * (a.tex5.x >= 0 && a.tex5.x < 32 ? pow(max(sampleTextureSlot(a.tex5.x, hit.uv, textures, material_sampler).rgb, float3(0.0f)), float3(2.2f)) : float3(1.0f));\n"
        "    surface.alpha = clamp(material.base_color.a, 0.0f, 1.0f); if (a.tex0.x >= 0 && a.tex0.x < 32) surface.alpha *= sampleTextureSlot(a.tex0.x, hit.uv, textures, material_sampler).a; if (a.tex1.z >= 0 && a.tex1.z < 32) surface.alpha *= sampleTextureSlot(a.tex1.z, hit.uv, textures, material_sampler).r;\n"
        "    surface.alpha_mode = int(a.misc.w + 0.5f); surface.unlit = a.misc.y > 0.5f; surface.double_sided = a.misc.z > 0.5f;\n"
        "    return surface;";
    replaceAdvancedSource(source, surface_begin, surface_advanced);

    replaceAdvancedSource(
        source,
        "float3 directBrdf(Surface surface, float3 view_direction, float3 light_direction, float3 incoming)\n{",
        "float3 dielectricF0(Surface surface) { float f=(surface.ior-1.0f)/(surface.ior+1.0f); return surface.specular_color*surface.specular_factor*(f*f); }\n"
        "float3 iridescentF0(Surface s, float3 f0, float cosine) { if(s.iridescence<=0.0f) return f0; float phase=s.iridescence_ior*s.iridescence_thickness*max(cosine,0.0f)*0.0125663706f; float3 film=0.5f+0.5f*cos(float3(phase,phase*1.29f+2.0944f,phase*1.61f+4.18879f)); return mix(f0,film,s.iridescence); }\n"
        "float anisotropicDistribution(Surface s, float3 n, float3 h) { if(s.anisotropy_strength<=1.0e-5f) return distributionGGX(n,h,s.roughness); float3 t=normalize(abs(n.y)<0.999f?cross(float3(0,1,0),n):cross(float3(1,0,0),n)); float3 b=normalize(cross(n,t)); float c=cos(s.anisotropy_rotation),q=sin(s.anisotropy_rotation); float3 tr=t*c+b*q,br=-t*q+b*c; float a=max(s.roughness*s.roughness,0.0025f),aspect=sqrt(max(1.0f-0.9f*s.anisotropy_strength,0.1f)); float ax=max(a/aspect,0.0025f),ay=max(a*aspect,0.0025f); float hx=dot(h,tr)/ax,hy=dot(h,br)/ay,hz=max(dot(h,n),0.0f),d=hx*hx+hy*hy+hz*hz; return 1.0f/max(PI*ax*ay*d*d,1.0e-6f); }\n"
        "float3 directBrdf(Surface surface, float3 view_direction, float3 light_direction, float3 incoming)\n{"
    );
    replaceAdvancedSource(source, "    float3 f0 = mix(float3(0.04f), surface.albedo, surface.metallic);\n", "    float3 f0 = mix(iridescentF0(surface, dielectricF0(surface), max(dot(n, v), 0.0f)), surface.albedo, surface.metallic);\n");
    replaceAdvancedSource(source, "    float d = distributionGGX(n, h, surface.roughness);\n", "    float d = anisotropicDistribution(surface, n, h);\n");
    replaceAdvancedSource(source, "        float coat_d = distributionGGX(n, h, max(surface.roughness * 0.35f, 0.04f));\n        float coat_g = geometrySmith(n, v, l, 0.25f);\n", "        float coat_roughness=max(surface.clearcoat_roughness,0.04f);\n        float coat_d=distributionGGX(n,h,coat_roughness);\n        float coat_g=geometrySmith(n,v,l,coat_roughness);\n");
    replaceAdvancedSource(source, "    return (diffuse + specular) * incoming * nl;\n", "    float sheen_grazing=pow(clamp(1.0f-max(dot(n,h),0.0f),0.0f,1.0f),1.0f+4.0f*surface.sheen_roughness);\n    return (diffuse + specular + surface.sheen_color*sheen_grazing) * incoming * nl;\n");

    replaceAdvancedSource(
        source,
        "    Surface surface = surfaceAt(hit, triangles, materials, advanced_materials, uniforms, textures, material_sampler);\n"
        "    float3 radiance = surface.emissive;",
        "    Surface surface = surfaceAt(hit, triangles, materials, advanced_materials, uniforms, textures, material_sampler);\n"
        "    if (surface.unlit) return applyEnvironmentFog(max(surface.albedo + surface.emissive, float3(0.0f)), hit.distance, gi_data);\n"
        "    float3 radiance = surface.emissive;"
    );
    replaceAdvancedSource(
        source,
        "    radiance += max(gi_data[3].rgb, float3(0.0f)) * max(gi_data[3].a, 0.0f) *\n"
        "        surface.albedo * (1.0f - surface.metallic) * surface.ao / PI;\n"
        "    return applyEnvironmentFog(max(radiance, float3(0.0f)), hit.distance, gi_data);",
        "    radiance += max(gi_data[3].rgb, float3(0.0f)) * max(gi_data[3].a, 0.0f) * surface.albedo * (1.0f - surface.metallic) * surface.ao / PI;\n"
        "    if (surface.transmission > 0.0f) {\n"
        "        float3 rg=refract(-view_direction,surface.normal,1.0f/surface.ior); if(dot(rg,rg)<=1.0e-8f) rg=-view_direction;\n"
        "        float3 transmitted=environmentRadiance(rg,surface.roughness,gi_data,textures,material_sampler);\n"
        "        float dispersion=surface.dispersion*0.01f; if(dispersion>0.0f){ float3 rr=refract(-view_direction,surface.normal,1.0f/max(surface.ior+dispersion,1.0001f)); float3 rb=refract(-view_direction,surface.normal,1.0f/max(surface.ior-dispersion,1.0001f)); if(dot(rr,rr)<=1.0e-8f)rr=rg; if(dot(rb,rb)<=1.0e-8f)rb=rg; transmitted=float3(environmentRadiance(rr,surface.roughness,gi_data,textures,material_sampler).r,transmitted.g,environmentRadiance(rb,surface.roughness,gi_data,textures,material_sampler).b);}\n"
        "        if(surface.thickness>0.0f && surface.attenuation_distance>1.0e-6f) transmitted*=pow(surface.attenuation_color,float3(surface.thickness/surface.attenuation_distance));\n"
        "        radiance=mix(radiance,transmitted+surface.emissive,surface.transmission);\n"
        "    }\n"
        "    if(surface.diffuse_transmission>0.0f) radiance+=max(gi_data[3].rgb,float3(0.0f))*max(gi_data[3].a,0.0f)*surface.diffuse_transmission_color*surface.albedo*surface.diffuse_transmission/PI;\n"
        "    return applyEnvironmentFog(max(radiance, float3(0.0f)), hit.distance, gi_data);"
    );
    return source;
}

} // namespace Renderer::Trace::Metal

#endif
