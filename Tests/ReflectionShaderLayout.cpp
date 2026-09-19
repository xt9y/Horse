#include <Renderer/SDLGPU/PBRShaders.hpp>

#include <cassert>
#include <string_view>

int main()
{
    const std::string_view shader = Renderer::SDLGPU::PBRShaders::Raster;

    assert(shader.find("Texture2D<float4> Tex13 : register(t13, space2)") != std::string_view::npos);
    assert(shader.find("Texture2D<float4> Tex14") == std::string_view::npos);
    assert(shader.find("Texture2DArray<float4> ReflectionProbes : register(t14, space2)") != std::string_view::npos);
    assert(shader.find("SamplerState ReflectionProbeSampler : register(s14, space2)") != std::string_view::npos);
    assert(shader.find("Texture2DArray<float> ShadowMaps : register(t15, space2)") != std::string_view::npos);
    assert(shader.find("StructuredBuffer<GpuBaseMaterial> PBaseMaterials : register(t16, space2)") != std::string_view::npos);
    assert(shader.find("StructuredBuffer<float4> PReflectionProbes : register(t23, space2)") != std::string_view::npos);
    assert(shader.find("PrefilteredEnvironmentColor") != std::string_view::npos);
    assert(shader.find("float3(u, v, 0.0)") != std::string_view::npos);
    assert(shader.find("PReflectionProbes[0].z") != std::string_view::npos);
    assert(shader.find("SampleReflectionProbe") != std::string_view::npos);
    assert(shader.find("BoxProjectedDirection") != std::string_view::npos);
    assert(shader.find("LocalReflectionColor") != std::string_view::npos);
    assert(shader.find("asint(meta.y)") != std::string_view::npos);

    return 0;
}
