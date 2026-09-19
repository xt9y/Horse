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

    const std::string submission =
        read("Sources/Renderer/Rasterizer/RasterDrawSubmissionSDLGPU.cpp");
    assert(submission.find("SDL_GPU_BUFFERUSAGE_INDEX") != std::string::npos);
    assert(submission.find("SDL_BindGPUIndexBuffer") != std::string::npos);
    assert(submission.find("SDL_GPU_INDEXELEMENTSIZE_32BIT") != std::string::npos);
    assert(submission.find("SDL_GPU_BUFFERUSAGE_INDIRECT") != std::string::npos);
    assert(submission.find("SDL_GPUIndexedIndirectDrawCommand") != std::string::npos);
    assert(submission.find("SDL_DrawGPUIndexedPrimitivesIndirect") != std::string::npos);
    assert(submission.find("batch.command_count") != std::string::npos);

    const std::string rasterizer =
        read("Sources/Renderer/Rasterizer/RasterizerSDLGPU.cpp");
    assert(rasterizer.find("RasterizerSDLGPU::DrawSubmission submission") != std::string::npos);
    assert(rasterizer.find("submission.sync(impl_->geometry") != std::string::npos);
    assert(rasterizer.find("submission.bindIndex(") != std::string::npos);
    assert(rasterizer.find("submission.batches()") != std::string::npos);
    assert(rasterizer.find("submission.drawIndirect(") != std::string::npos);
    assert(rasterizer.find("SDL_DrawGPUIndexedPrimitives(") != std::string::npos);
    assert(rasterizer.find("draw.first_vertex, UINT32_MAX") != std::string::npos);
    assert(rasterizer.find("RasterDrawUniforms draw_uniforms{\n                static_cast<std::uint32_t>(std::min<std::size_t>(draw.first_vertex") == std::string::npos);

    return 0;
}
