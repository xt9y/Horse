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
    const std::string components = read("Sources/Renderer/Components.hpp");
    const std::string scene = read("Sources/Renderer/Scenes/Scene.hpp");
    const std::string cache = read("Sources/Renderer/Scenes/SceneCache.hpp");
    const std::string progressive = read("Sources/Renderer/Systems/ProgressiveState.hpp");
    const std::string uniforms = read("Sources/Renderer/Systems/Uniforms.hpp");
    const std::string program = read("Sources/Renderer/Systems/OpenGL/Program.hpp");
    const std::string textures = read("Sources/Renderer/Systems/OpenGL/TextureCache.hpp");
    const std::string gl_resources = read("Sources/Renderer/Scenes/OpenGL/SceneResources.cpp");
    const std::string metal_resources = read("Sources/Renderer/Scenes/Metal/SceneResources.hpp");
    const std::string gi = read("Sources/Renderer/GlobalIllumination/GlobalIllumination.cpp");
    const std::string gi_gl = read("Sources/Renderer/GlobalIllumination/OpenGL/GlobalIlluminationOpenGL.cpp");
    const std::string gi_metal = read("Sources/Renderer/GlobalIllumination/Metal/GlobalIlluminationMetal.cpp");
    const std::string photon_header = read("Sources/Renderer/GlobalIllumination/PhotonMapping/PhotonMap.hpp");
    const std::string photon_source = read("Sources/Renderer/GlobalIllumination/PhotonMapping/PhotonMap.cpp");
    const std::string trace_scene = read("Sources/Renderer/GlobalIllumination/TraceScene.hpp");
    const std::string font_pass = read("Sources/Renderer/Fonts/FontPass.hpp");
    const std::string font_gl = read("Sources/Renderer/Fonts/OpenGL/FontPassOpenGL.cpp");
    const std::string font_metal = read("Sources/Renderer/Fonts/Metal/FontPassMetal.cpp");
    const std::string rasterizer = read("Sources/Renderer/Rasterizer/OpenGL/RasterizerOpenGL.cpp");
    const std::string pathtracer = read("Sources/Renderer/PathTracer/OpenGL/PathTracerOpenGL.cpp");
    const std::string pathtracer_metal = read("Sources/Renderer/PathTracer/Metal/PathTracerMetal.cpp");
    const std::string raytracer = read("Sources/Renderer/RayTracer/OpenGL/RayTracerOpenGL.cpp");
    const std::string raytracer_metal = read("Sources/Renderer/RayTracer/Metal/RayTracerMetal.cpp");
    const std::string raytracer_metal_shaders = read("Sources/Renderer/RayTracer/Metal/RayTracerMetalShaders.hpp");
    const std::string raytracer_header = read("Sources/Renderer/RayTracer/RayTracer.hpp");
    const std::string render = read("Sources/Renderer/Render.hpp");
    const std::string build = read("build.c");

    assert(camera.find("world.markChanged()") == std::string::npos);

    assert(scene.find("namespace Renderer::Scenes") != std::string::npos);
    assert(cache.find("namespace Renderer::Scenes") != std::string::npos);
    assert(cache.find("class SceneCache") != std::string::npos);
    assert(cache.find("struct GpuNode") != std::string::npos);
    assert(cache.find("struct GpuTriangle") != std::string::npos);
    assert(cache.find("struct GpuMaterial") != std::string::npos);
    assert(progressive.find("class ProgressiveState") != std::string::npos);
    assert(uniforms.find("struct alignas(16) MetalTraceUniforms") != std::string::npos);

    assert(program.find("namespace Renderer::Systems::OpenGL") != std::string::npos);
    assert(program.find("class Program") != std::string::npos);
    assert(textures.find("class TextureCache") != std::string::npos);
    assert(gl_resources.find("namespace Renderer::Scenes::OpenGL") != std::string::npos);
    assert(metal_resources.find("namespace Renderer::Scenes::Metal") != std::string::npos);
    assert(metal_resources.find("class SceneResources") != std::string::npos);

    assert(gi.find("PhotonMapping::PhotonMap") != std::string::npos);
    assert(gi.find("photon_map.rebuild") != std::string::npos);
    assert(gi.find("photon_map.sample") != std::string::npos);
    assert(gi_gl.find("uploaded_revision") != std::string::npos);
    assert(gi_gl.find("revision == uploaded_revision") != std::string::npos);
    assert(gi_metal.find("uploaded_revision") != std::string::npos);
    assert(gi_metal.find("revision == uploaded_revision") != std::string::npos);

    assert(trace_scene.find("class TraceScene") != std::string::npos);
    assert(trace_scene.find("const Scenes::SceneCache& cache() const") != std::string::npos);
    assert(photon_header.find("class PhotonMap") != std::string::npos);
    assert(photon_header.find("struct Settings") != std::string::npos);
    assert(photon_header.find("void rebuild") != std::string::npos);
    assert(photon_header.find("Vec3 sample") != std::string::npos);
    assert(photon_source.find("std::unordered_map<Cell") != std::string::npos);
    assert(photon_source.find("cosineHemisphere") != std::string::npos);
    assert(components.find("bool photon_mapping = true;") != std::string::npos);
    assert(components.find("std::uint32_t photon_count = 4096u;") != std::string::npos);
    assert(components.find("float photon_radius = 0.0f;") != std::string::npos);

    assert(font_pass.find("namespace Renderer::Internal") != std::string::npos);
    assert(font_gl.find("Renderer/Fonts/FontPass.hpp") != std::string::npos);
    assert(font_metal.find("Renderer/Fonts/FontPass.hpp") != std::string::npos);

    assert(rasterizer.find("Renderer/Scenes/Scene.hpp") != std::string::npos);
    assert(pathtracer.find("Renderer/Scenes/") != std::string::npos);
    assert(pathtracer_metal.find("Renderer/Scenes/") != std::string::npos);
    assert(raytracer.find("Renderer/Scenes/") != std::string::npos);
    assert(raytracer_metal.find("Renderer/Scenes/") != std::string::npos);

    assert(!exists("Sources/Renderer/FontAtlas.cpp"));
    assert(!exists("Sources/Renderer/FontPassMetal.cpp"));
    assert(!exists("Sources/Renderer/GlobalIllumination.cpp"));
    assert(!exists("Sources/Renderer/GlobalIlluminationMetal.cpp"));
    assert(!exists("Sources/Renderer/Scene.hpp"));
    assert(!exists("Sources/Renderer/Systems/Scene.cpp"));
    assert(!exists("Sources/Renderer/Systems/MetalSceneResources.cpp"));
    assert(!exists("Sources/Renderer/Systems/OpenGLSceneResources.cpp"));
    assert(!exists("Sources/Renderer/PathTracer/PathTracer.cpp"));
    assert(!exists("Sources/Renderer/PathTracer/PathTracerMetal.cpp"));
    assert(!exists("Sources/Renderer/RayTracer/RayTracer.cpp"));
    assert(!exists("Sources/Renderer/RayTracer/RayTracerMetal.cpp"));

    assert(raytracer_header.find("class RayTracer final : public IRenderer") != std::string::npos);
    assert(raytracer_header.find("int resolution_divisor = 4;") != std::string::npos);
    assert(raytracer.find("std::clamp(settings.resolution_divisor, 4, 8)") != std::string::npos);
    assert(raytracer_metal.find("std::clamp(settings.resolution_divisor, 4, 8)") != std::string::npos);
    assert(raytracer_metal.find("Renderer/RayTracer/Metal/RayTracerMetalShaders.hpp") != std::string::npos);
    assert(raytracer_metal.find("Metal.createTriangleAccelerationStructure") != std::string::npos);
    assert(raytracer_metal.find("Metal.setAccelerationStructure") != std::string::npos);
    assert(raytracer_metal.find("meaningful_alpha") != std::string::npos);
    assert(raytracer_metal.find("trace.counts[3]") != std::string::npos);
    assert(raytracer_metal_shaders.find("#include <metal_raytracing>") != std::string::npos);
    assert(raytracer_metal_shaders.find("primitive_acceleration_structure") != std::string::npos);
    assert(raytracer_metal_shaders.find("intersector<triangle_data>") != std::string::npos);
    assert(raytracer_metal_shaders.find("assume_geometry_type(geometry_type::triangle)") != std::string::npos);
    assert(raytracer_metal_shaders.find("assume_identity_transforms(true)") != std::string::npos);
    assert(raytracer_metal_shaders.find("accept_any_intersection(true)") != std::string::npos);
    assert(raytracer_metal_shaders.find("uniforms.counts.w") != std::string::npos);
    assert(raytracer_metal_shaders.find("deterministicDepthAlpha") == std::string::npos);
    assert(render.find("Renderer/RayTracer/RayTracer.hpp") != std::string::npos);

    assert(build.find("Sources/Renderer/Fonts/FontPass.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/GlobalIllumination/GlobalIllumination.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/GlobalIllumination/PhotonMapping/PhotonMap.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/Scenes/Scene.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/Scenes/SceneCache.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/PathTracer/OpenGL/PathTracerOpenGL.cpp") != std::string::npos);
    assert(build.find("Sources/Renderer/PathTracer/Metal/PathTracerMetal.cpp") != std::string::npos);
    assert(build.find("renderer-systems-contract") != std::string::npos);
    return 0;
}
