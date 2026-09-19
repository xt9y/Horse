#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

int main()
{
    const std::filesystem::path source =
        std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Sources/Renderer/Volumetrics/VolumetricsSDLGPU.cpp";

    std::ifstream file(source);
    assert(file);
    std::ostringstream stream;
    stream << file.rdbuf();
    const std::string text = stream.str();

    const std::size_t begin = text.find("inline constexpr const char *MarchShader");
    const std::size_t end = text.find("inline constexpr const char *BlurShader", begin);
    assert(begin != std::string::npos);
    assert(end != std::string::npos);
    const std::string march = text.substr(begin, end - begin);

    assert(march.find("Texture2D<float> SceneDepth : register(t0, space0)") != std::string::npos);
    assert(march.find("SamplerState DepthSampler : register(s0, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuNode> Nodes : register(t1, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuTriangle> Triangles : register(t2, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<float4> Shading : register(t3, space0)") != std::string::npos);
    assert(march.find("RWTexture2D<float4> Output : register(u0, space1)") != std::string::npos);
    assert(march.find("cbuffer FrameData : register(b0, space2)") != std::string::npos);
    assert(march.find("cbuffer VolumetricData : register(b1, space2)") != std::string::npos);
    assert(march.find("space3") == std::string::npos);

    assert(text.find("bool pipelines_attempted = false;") != std::string::npos);
    assert(text.find("if (state.pipelines_attempted) return false;") != std::string::npos);
    assert(text.find("state.pipelines_attempted = true;") != std::string::npos);

    return 0;
}
