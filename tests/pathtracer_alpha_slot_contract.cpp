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
    const std::string scene = read("Sources/Renderer/Scene.cpp");
    const std::string gl = read("Sources/Renderer/PathTracer/PathTracer.cpp");
    const std::string metal = read("Sources/Renderer/PathTracer/PathTracerMetal.cpp");

    assert(scene.find("std::stable_partition") != std::string::npos);
    assert(scene.find("meaningful_alpha") != std::string::npos);
    assert(scene.find("Models::texture") != std::string::npos);

    assert(gl.find("kMaximumTextureSlots = 16u") != std::string::npos);
    assert(metal.find("kMaximumTextureSlots = 16u") != std::string::npos);
    return 0;
}
