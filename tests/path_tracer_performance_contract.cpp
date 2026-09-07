#include "Sources/Renderer/PathTracer/PathTracerShaders.hpp"

#include <cassert>
#include <string_view>

int main()
{
    const std::string_view shader(Renderer::PathTracerShaders::trace);

    // GPU traversal must remain stackless. Large per-invocation BVH stacks can
    // spill into local memory and destroy compute occupancy.
    assert(shader.find("uint stack[") == std::string_view::npos);

    // The path tracer should walk one cached world BVH, not nested TLAS->BLAS
    // traversal for every material-split Sponza mesh.
    assert(shader.find("tlas_nodes") == std::string_view::npos);
    assert(shader.find("instances[") == std::string_view::npos);

    return 0;
}
