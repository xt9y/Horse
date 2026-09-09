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
    const std::string gl_upload = read("Sources/Renderer/PathTracer/PathTracer.cpp");
    const std::string metal_upload = read("Sources/Renderer/PathTracer/PathTracerMetal.cpp");

    assert(gl.find("alphaCutoutPass") != std::string::npos);
    assert(gl.find("material.data.y") != std::string::npos);
    assert(gl.find("if (!alphaCutoutPass") != std::string::npos);
    assert(metal.find("alphaCutoutPass") != std::string::npos);
    assert(metal.find("material.data.y") != std::string::npos);
    assert(metal.find("if (!alphaCutoutPass") != std::string::npos);

    assert(gl_upload.find("meaningful_alpha") != std::string::npos);
    assert(gl_upload.find("gpu_material.data[1]") != std::string::npos);
    assert(metal_upload.find("meaningful_alpha") != std::string::npos);
    assert(metal_upload.find("gpu_material.data[1]") != std::string::npos);
    return 0;
}
