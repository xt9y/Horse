#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

int main()
{
    const std::filesystem::path source =
        std::filesystem::path(__FILE__).parent_path().parent_path() /
        "Sources/Renderer/Rasterizer/RasterGeometrySDLGPU.cpp";

    std::ifstream file(source);
    assert(file);

    std::ostringstream stream;
    stream << file.rdbuf();
    const std::string text = stream.str();

    const std::size_t begin = text.find("std::uint64_t topologySignature(");
    const std::size_t end = text.find("std::uint64_t cameraLayerSignature(", begin);
    assert(begin != std::string::npos);
    assert(end != std::string::npos);

    const std::string topology = text.substr(begin, end - begin);

    assert(topology.find("Models::resourceRevision()") == std::string::npos);
    assert(topology.find("Models::Internal::meshRevision") != std::string::npos);
    return 0;
}
