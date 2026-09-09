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

static bool exists(const char *path)
{
    std::ifstream file(path);
    return file.good();
}

int main()
{
    const std::string camera = read("Sources/Camera.cpp");
    const std::string scene = read("Sources/Renderer/Systems/Scene.hpp");
    const std::string scene_compat = read("Sources/Renderer/Scene.hpp");
    const std::string cache = read("Sources/Renderer/Systems/SceneCache.hpp");
    const std::string progressive = read("Sources/Renderer/Systems/ProgressiveState.hpp");
    const std::string uniforms = read("Sources/Renderer/Systems/Uniforms.hpp");
    const std::string program = read("Sources/Renderer/Systems/OpenGL/Program.hpp");
    const std::string textures = read("Sources/Renderer/Systems/OpenGL/TextureCache.hpp");
    const std::string gl_resources = read("Sources/Renderer/Systems/OpenGLSceneResources.cpp");
    const std::string metal_resources = read("Sources/Renderer/Systems/MetalSceneResources.hpp");
    const std::string rasterizer = read("Sources/Renderer/Rasterizer/Rasterizer.cpp");
    const std::string pathtracer = read("Sources/Renderer/PathTracer/PathTracer.cpp");
    const std::string pathtracer_metal = read("Sources/Renderer/PathTracer/PathTracerMetal.cpp");
    const std::string raytracer = read("Sources/Renderer/RayTracer/RayTracer.cpp");
    const std::string raytracer_metal = read("Sources/Renderer/RayTracer/RayTracerMetal.cpp");
    const std::string raytracer_metal_shaders = read("Sources/Renderer/RayTracer/RayTracerMetalShaders.hpp");
    const std::string raytracer_header = read("Sources/Renderer/RayTracer/RayTracer.hpp");
    const std::string render = read("Sources/Renderer/Render.hpp");
    const std::string build = read("build.c");

    assert(camera.find("world.markChanged()") == std::string::npos);

    assert(scene.find("namespace Renderer::Systems::Scene") != std::string::npos);
    assert(scene_compat.find("namespace Scene = Systems::Scene") != std::string::npos);

    assert(cache.find("namespace Renderer::Systems") != std::string::npos);
    assert(cache.find("class SceneCache") != std::string::npos);
    assert(cache.find("struct GpuNode") != std::string::npos);
    assert(cache.find("struct GpuTriangle") != std::string::npos);
    assert(cache.find("struct GpuMaterial") != std::string::npos);
    assert(progressive.find("class ProgressiveState") != std::string::npos);
    assert(uniforms.find("struct alignas(16) MetalTraceUniforms") != std::string::npos);

    assert(program.find("namespace Renderer::Systems::OpenGL") != std::string::npos);
    assert(program.find("class Program") != std::string::npos);
    assert(program.find("bool createGraphics") != std::string::npos);
    assert(program.find("bool createCompute") != std::string::npos);
    assert(textures.find("class TextureCache") != std::string::npos);
    assert(gl_resources.find("Systems/OpenGL/TextureCache.hpp") != std::string::npos);
    assert(metal_resources.find("class MetalSceneResources") != std::string::npos);

    assert(rasterizer.find("Renderer/Systems/Scene.hpp") != std::string::npos);
    assert(rasterizer.find("Renderer/Systems/OpenGL/Program.hpp") != std::string::npos);
    assert(rasterizer.find("Renderer/Systems/OpenGL/TextureCache.hpp") != std::string::npos);
    assert(pathtracer.find("Renderer/Systems/") != std::string::npos);
    assert(pathtracer_metal.find("Renderer/Systems/") != std::string::npos);
    assert(raytracer.find("Renderer/Systems/") != std::string::npos);
    assert(raytracer_metal.find("Renderer/Systems/") != std::string::npos);

    assert(rasterizer.find("Renderer/RayTracing/") == std::string::npos);
    assert(pathtracer.find("Renderer/RayTracing/") == std::string::npos);
    assert(pathtracer_metal.find("Renderer/RayTracing/") == std::string::npos);
    assert(raytracer.find("Renderer/RayTracing/") == std::string::npos);
    assert(raytracer_metal.find("Renderer/RayTracing/") == std::string::npos);
    assert(!exists("Sources/Renderer/RayTracing/Compatibility.hpp"));

    assert(raytracer_header.find("class RayTracer final : public IRenderer") != std::string::npos);
    assert(raytracer_header.find("struct RayTracerSettings") != std::string::npos);
    assert(raytracer_header.find("int resolution_divisor = 4;") != std::string::npos);
    assert(raytracer.find("std::clamp(settings.resolution_divisor, 4, 8)") != std::string::npos);
    assert(raytracer_metal.find("std::clamp(settings.resolution_divisor, 4, 8)") != std::string::npos);
    assert(raytracer_metal.find("Renderer/RayTracer/RayTracerMetalShaders.hpp") != std::string::npos);
    assert(raytracer_metal.find("Renderer/PathTracer/PathTracerMetalShaders.hpp") == std::string::npos);
    assert(raytracer_metal.find("Metal.createTriangleAccelerationStructure") != std::string::npos);
    assert(raytracer_metal.find("Metal.setAccelerationStructure") != std::string::npos);
    assert(raytracer_metal_shaders.find("#include <metal_raytracing>") != std::string::npos);
    assert(raytracer_metal_shaders.find("primitive_acceleration_structure") != std::string::npos);
    assert(raytracer_metal_shaders.find("intersector<triangle_data>") != std::string::npos);
    assert(raytracer_metal_shaders.find("assume_geometry_type(geometry_type::triangle)") != std::string::npos);
    assert(raytracer_metal_shaders.find("assume_identity_transforms(true)") != std::string::npos);
    assert(raytracer_metal_shaders.find("deterministicDepthAlpha") == std::string::npos);
    assert(render.find("Renderer/RayTracer/RayTracer.hpp") != std::string::npos);

    assert(build.find("Sources/Renderer/Systems/Scene.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/Systems/SceneCache.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/Systems/OpenGL/Program.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/Systems/OpenGL/TextureCache.cpp") != std::string::npos);
    assert(build.find("renderer-systems-contract") != std::string::npos);
    return 0;
}
