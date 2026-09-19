#include <Renderer/SDLGPU/PBRShaders.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string read(const char *path)
{
    const std::filesystem::path source =
        std::filesystem::path(__FILE__).parent_path().parent_path() / path;
    std::ifstream file(source);
    assert(file);
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

} // namespace

int main()
{
    const std::string_view shader = Renderer::SDLGPU::PBRShaders::Raster;
    assert(shader.find("cbuffer VertexDraw") != std::string_view::npos);
    assert(shader.find("VVertices[vertex_id + VDraw.x]") != std::string_view::npos);

    const std::string geometry =
        read("Sources/Renderer/Rasterizer/RasterGeometrySDLGPU.cpp");
    assert(geometry.find("draw_items.push_back") != std::string::npos);
    assert(geometry.find("RasterGeometry::drawItems() const") != std::string::npos);

    const std::string submission =
        read("Sources/Renderer/Rasterizer/RasterDrawSubmissionSDLGPU.cpp");
    assert(submission.find("SDL_GPU_BUFFERUSAGE_INDEX") != std::string::npos);
    assert(submission.find("SDL_BindGPUIndexBuffer") != std::string::npos);
    assert(submission.find("SDL_GPU_INDEXELEMENTSIZE_32BIT") != std::string::npos);
    assert(submission.find("SDL_GPU_BUFFERUSAGE_INDIRECT") != std::string::npos);
    assert(submission.find("SDL_GPUIndexedIndirectDrawCommand") != std::string::npos);
    assert(submission.find("SDL_DrawGPUIndexedPrimitivesIndirect") != std::string::npos);
    assert(submission.find("SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE") != std::string::npos);
    assert(submission.find("StructuredBuffer<uint4> Draws : register(t0, space0)") != std::string::npos);
    assert(submission.find("StructuredBuffer<uint> Visibility : register(t1, space0)") != std::string::npos);
    assert(submission.find("RWStructuredBuffer<uint> Indices : register(u0, space1)") != std::string::npos);
    assert(submission.find("RWStructuredBuffer<DrawCommand> Commands : register(u1, space1)") != std::string::npos);
    assert(submission.find("InterlockedAdd(Commands[draw.w].num_indices") != std::string::npos);
    assert(submission.find("SV_GroupID") != std::string::npos);
    assert(submission.find("SV_GroupThreadID") != std::string::npos);
    assert(submission.find("SDL_BeginGPUComputePass") != std::string::npos);
    assert(submission.find("Models::AlphaMode::Blend") != std::string::npos);
    assert(submission.find("gpu_eligible") != std::string::npos);

    const std::string rasterizer =
        read("Sources/Renderer/Rasterizer/RasterizerSDLGPU.cpp");
    assert(rasterizer.find("RasterizerSDLGPU::DrawSubmission submission") != std::string::npos);
    assert(rasterizer.find("submission.sync(impl_->geometry") != std::string::npos);
    assert(rasterizer.find("submission.compact(command, impl_->visibility_buffer") != std::string::npos);
    assert(rasterizer.find("submission.bindIndex(") != std::string::npos);
    assert(rasterizer.find("submission.batches()") != std::string::npos);
    assert(rasterizer.find("submission.drawIndirect(") != std::string::npos);
    assert(rasterizer.find("SDL_DrawGPUIndexedPrimitives(") != std::string::npos);
    assert(rasterizer.find("draw.first_vertex, UINT32_MAX") != std::string::npos);
    assert(rasterizer.find("SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ") != std::string::npos);
    assert(rasterizer.find("RasterDrawUniforms draw_uniforms{\n                static_cast<std::uint32_t>(std::min<std::size_t>(draw.first_vertex") == std::string::npos);

    return 0;
}
