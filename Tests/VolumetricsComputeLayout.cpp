#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read(const std::filesystem::path& path)
{
    std::ifstream file(path);
    assert(file);
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::string march = read(
        root / "Sources/Renderer/Internal/VolumetricMarchShader.hpp"
    );
    const std::string implementation = read(
        root / "Sources/Renderer/Volumetrics/VolumetricsSDLGPU.cpp"
    );

    assert(march.find("Texture2D<float> SceneDepth : register(t0, space0)") != std::string::npos);
    assert(march.find("SamplerState DepthSampler : register(s0, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuNode> TlasNodes : register(t1, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuInstance> Instances : register(t2, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuNode> BlasNodes : register(t3, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<uint4> Blases : register(t4, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuTriangle> LocalTriangles : register(t5, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuNode> DynamicNodes : register(t6, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<GpuTriangle> DynamicTriangles : register(t7, space0)") != std::string::npos);
    assert(march.find("StructuredBuffer<float4> Shading : register(t8, space0)") != std::string::npos);
    assert(march.find("RWTexture2D<float4> Output : register(u0, space1)") != std::string::npos);
    assert(march.find("cbuffer FrameData : register(b0, space2)") != std::string::npos);
    assert(march.find("cbuffer VolumetricData : register(b1, space2)") != std::string::npos);
    assert(march.find("cbuffer AccelerationData : register(b2, space2)") != std::string::npos);
    assert(march.find("OccludedTlas") != std::string::npos);
    assert(march.find("OccludedBlas") != std::string::npos);
    assert(march.find("OccludedDynamic") != std::string::npos);
    assert(march.find("space3") == std::string::npos);

    assert(implementation.find("VolumetricMarchShader") != std::string::npos);
    assert(implementation.find("state.acceleration_gpu.bindCompute(pass, 0u)") != std::string::npos);
    assert(implementation.find("bindGlobalIlluminationSDLGPU(pass, global_illumination, 7u)") != std::string::npos);
    assert(implementation.find("SDL_PushGPUComputeUniformData(\n        command,\n        2u,") != std::string::npos);
    assert(implementation.find("bool pipelines_attempted = false;") != std::string::npos);
    assert(implementation.find("if (state.pipelines_attempted) return false;") != std::string::npos);
    assert(implementation.find("state.pipelines_attempted = true;") != std::string::npos);

    return 0;
}
