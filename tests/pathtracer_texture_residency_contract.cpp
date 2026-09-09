#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

static std::string read(const char *path)
{
    std::ifstream file(path);
    assert(file.good());
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main()
{
    const std::string metal = read("Sources/Renderer/PathTracer/PathTracerMetal.cpp");
    const std::string shader = read("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp");

    assert(metal.find("kMaximumTextureSlots = 32u") != std::string::npos);
    assert(metal.find("Metal texture capacity exceeded") != std::string::npos);
    assert(metal.find("Metal.setTexture(command, primary_depth, 33u)") != std::string::npos);

    assert(shader.find("array<texture2d<float>, 32>") != std::string::npos);
    assert(shader.find("slot < 32") != std::string::npos);
    assert(shader.find("case 31:") != std::string::npos);
    assert(shader.find("[[texture(33)]]") != std::string::npos);
    return 0;
}
