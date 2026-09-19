#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
    const std::string rasterizer =
        read("Sources/Renderer/Rasterizer/RasterizerSDLGPU.cpp");

    assert(rasterizer.find("Renderer/RenderGraph/RenderGraph.hpp") != std::string::npos);
    assert(rasterizer.find("RenderGraph::Graph graph") != std::string::npos);
    assert(rasterizer.find("graph.resource(\"Depth\")") != std::string::npos);
    assert(rasterizer.find("graph.resource(\"Hi-Z\")") != std::string::npos);
    assert(rasterizer.find("graph.resource(\"Visibility\", true)") != std::string::npos);
    assert(rasterizer.find("graph.resource(\"Ambient Occlusion\")") != std::string::npos);
    assert(rasterizer.find("graph.resource(\"Lighting\", true)") != std::string::npos);
    assert(rasterizer.find("graph.pass(\"Depth\"") != std::string::npos);
    assert(rasterizer.find("graph.pass(\"Hi-Z\"") != std::string::npos);
    assert(rasterizer.find("graph.pass(\"Visibility\"") != std::string::npos);
    assert(rasterizer.find("graph.pass(\"GPU Draw Compaction\"") != std::string::npos);
    assert(rasterizer.find("graph.pass(\"Ambient Occlusion\"") != std::string::npos);
    assert(rasterizer.find("graph.pass(\"Forward+\"") != std::string::npos);
    assert(rasterizer.find("graph.execute(&error)") != std::string::npos);

    return 0;
}
