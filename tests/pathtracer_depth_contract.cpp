#include "Renderer/PathTracer/PathTracerShaders.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <cassert>
#include <cstring>

int main()
{
    assert(std::strstr(Renderer::PathTracerShaders::trace, "uPrimaryDepth") != nullptr);
    assert(std::strstr(Renderer::PathTracerShaders::trace, "imageStore(uPrimaryDepth") != nullptr);
    assert(std::strstr(Renderer::PathTracerMetalShaders::source, "primary_depth") != nullptr);
    assert(std::strstr(Renderer::PathTracerMetalShaders::source, "primary_depth.write") != nullptr);
    return 0;
}
