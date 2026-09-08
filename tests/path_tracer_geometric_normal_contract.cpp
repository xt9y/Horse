#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

static std::string readFile(const char *path)
{
    std::ifstream input(path);
    assert(input.good());
    return std::string(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
}

int main()
{
    const std::string gl = readFile("Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp");
    const std::string metal = readFile("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp");

    // Shading normals are valid for BRDF evaluation, but using them to offset
    // secondary rays can move the origin underneath the actual triangle plane
    // and produce projected triangle-shaped self-shadowing.
    assert(gl.find("vec3 geometric_normal;") != std::string::npos);
    assert(gl.find("best.geometric_normal = normalize(cross(") != std::string::npos);
    assert(gl.find("shadow_origin = hit.position + hit.geometric_normal") != std::string::npos);
    assert(gl.find("origin = hit.position + hit.geometric_normal") != std::string::npos);
    assert(gl.find("dot(direction, hit.geometric_normal) <= 0.0") != std::string::npos);

    assert(metal.find("float3 geometric_normal;") != std::string::npos);
    assert(metal.find("best.geometric_normal = normalize(cross(") != std::string::npos);
    assert(metal.find("shadow_origin = hit.position + hit.geometric_normal") != std::string::npos);
    assert(metal.find("origin = hit.position + hit.geometric_normal") != std::string::npos);
    assert(metal.find("dot(direction, hit.geometric_normal) <= 0.0f") != std::string::npos);

    // Keep the interpolated shading normal for BRDF lighting itself.
    assert(gl.find("dot(hit.normal, light_direction)") != std::string::npos);
    assert(metal.find("dot(hit.normal, light_direction)") != std::string::npos);

    return 0;
}
