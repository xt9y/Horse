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
    assert(shader.find("cbuffer VertexDraw") == std::string_view::npos);
    assert(shader.find("VDraw.x") == std::string_view::npos);
    assert(shader.find("VVertices[vertex_id]") != std::string_view::npos);

    const std::string submission =
        read("Sources/Renderer/Rasterizer/RasterDrawSubmissionSDLGPU.cpp");
    assert(submission.find("SDL_GPU_BUFFERUSAGE_INDEX") != std::string::npos);
    assert(submission.find("SDL_BindGPUIndexBuffer") != std::string::npos);
    assert(submission.find("SDL_GPU_INDEXELEMENTSIZE_32BIT") != std::string::npos);

    const std::string rasterizer =
        read("Sources/Renderer/Rasterizer/RasterizerSDLGPU.cpp");
    assert(rasterizer.find("SDL_DrawGPUIndexedPrimitives(") != std::string::npos);
    assert(rasterizer.find("RasterDrawUniforms") == std::string::npos);
    assert(rasterizer.find("SDL_PushGPUVertexUniformData(\n                command, 1u") == std::string::npos);

    return 0;
}
