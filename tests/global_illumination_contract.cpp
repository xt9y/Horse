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
    const std::string components = read("Sources/Renderer/Components.hpp");
    const std::string renderer_h = read("Sources/Renderer/Renderer.hpp");
    const std::string renderer_cpp = read("Sources/Renderer/Renderer.cpp");
    const std::string rasterizer = read("Sources/Renderer/Rasterizer/Rasterizer.cpp");
    const std::string pathtracer_h = read("Sources/Renderer/PathTracer/PathTracer.hpp");
    const std::string pathtracer_glsl = read("Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp");
    const std::string pathtracer_metal = read("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp");

    assert(components.find("struct GlobalIlluminationComponent") != std::string::npos);
    assert(components.find("std::uint8_t bounces = 2") != std::string::npos);
    assert(renderer_h.find("const GlobalIllumination::Field *global_illumination") != std::string::npos);
    assert(renderer_cpp.find("GlobalIllumination::update(world)") != std::string::npos);
    assert(rasterizer.find("GlobalIllumination::sample") != std::string::npos);
    assert(pathtracer_h.find("max_bounces") == std::string::npos);
    assert(pathtracer_glsl.find("sampleGlobalIllumination") != std::string::npos);
    assert(pathtracer_metal.find("sampleGlobalIllumination") != std::string::npos);
    return 0;
}
