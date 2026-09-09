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
    const std::string gi = read("Sources/Renderer/GlobalIllumination.cpp");
    const std::string renderer_h = read("Sources/Renderer/Renderer.hpp");
    const std::string renderer_cpp = read("Sources/Renderer/Renderer.cpp");
    const std::string rasterizer = read("Sources/Renderer/Rasterizer/Rasterizer.cpp");
    const std::string pathtracer_h = read("Sources/Renderer/PathTracer/PathTracer.hpp");
    const std::string pathtracer_gl = read("Sources/Renderer/PathTracer/PathTracer.cpp");
    const std::string pathtracer_metal = read("Sources/Renderer/PathTracer/PathTracerMetal.cpp");
    const std::string pathtracer_glsl = read("Sources/Renderer/PathTracer/PathTracerSharedGITrace.hpp");
    const std::string pathtracer_msl = read("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp");
    const std::string gi_gl = read("Sources/Renderer/GlobalIlluminationOpenGL.cpp");
    const std::string gi_metal = read("Sources/Renderer/GlobalIlluminationMetal.cpp");

    assert(components.find("struct GlobalIlluminationComponent") != std::string::npos);
    assert(components.find("std::uint8_t bounces = 2") != std::string::npos);
    assert(components.find("float intensity = 1.0f") != std::string::npos);

    assert(renderer_h.find("const GlobalIllumination::Field *global_illumination") != std::string::npos);
    assert(renderer_cpp.find("GlobalIllumination::update(world)") != std::string::npos);
    assert(gi.find("kProbeBudgetPerFrame") != std::string::npos);
    assert(gi.find("geometrySignature") != std::string::npos);
    assert(gi.find("lightSignature") != std::string::npos);
    assert(gi.find("camera") == std::string::npos);

    assert(rasterizer.find("GlobalIllumination::sample") != std::string::npos);

    assert(pathtracer_h.find("max_bounces") == std::string::npos);
    assert(pathtracer_gl.find("settings.max_bounces") == std::string::npos);
    assert(pathtracer_metal.find("settings.max_bounces") == std::string::npos);
    assert(pathtracer_gl.find("globalIlluminationSignature") != std::string::npos);
    assert(pathtracer_metal.find("globalIlluminationSignature") != std::string::npos);

    assert(pathtracer_glsl.find("layout(std430, binding = 5) readonly buffer SharedGiField") != std::string::npos);
    assert(pathtracer_glsl.find("sampleGlobalIllumination") != std::string::npos);
    assert(pathtracer_glsl.find("cosineHemisphere") == std::string::npos);

    assert(pathtracer_msl.find("device const float4 *gi_data [[buffer(4)]]") != std::string::npos);
    assert(pathtracer_msl.find("sampleGlobalIllumination") != std::string::npos);
    assert(pathtracer_msl.find("cosineHemisphere") == std::string::npos);

    assert(gi_gl.find("kHeaderVec4Count = 4u") != std::string::npos);
    assert(gi_gl.find("kGiBinding = 5u") != std::string::npos);
    assert(gi_metal.find("kHeaderVec4Count = 4u") != std::string::npos);
    assert(pathtracer_metal.find("bindGlobalIlluminationMetal(command, global_illumination, 4u)") != std::string::npos);

    return 0;
}
