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
    const std::string gl = read("Sources/Renderer/PathTracer/PathTracerSharedGITrace.hpp");
    const std::string metal = read("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp");

    assert(gl.find("alphaCutoutPass") != std::string::npos);
    assert(gl.find("material.base_color.a") != std::string::npos);
    assert(gl.find("texel.a") != std::string::npos);
    assert(gl.find("if (!alphaCutoutPass") != std::string::npos);

    assert(metal.find("alphaCutoutPass") != std::string::npos);
    assert(metal.find("material.base_color.a") != std::string::npos);
    assert(metal.find("texel.a") != std::string::npos);
    assert(metal.find("if (!alphaCutoutPass") != std::string::npos);
    return 0;
}
