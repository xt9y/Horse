#include "Sources/Renderer/PathTracer/PathTracer.hpp"
#include "Sources/Renderer/PathTracer/PathTracerShaders.hpp"

#include <cassert>
#include <string_view>

int main()
{
    const std::string_view shader(Renderer::PathTracerShaders::trace);

    assert(shader.find("uint stack[") == std::string_view::npos);
    assert(shader.find("tlas_nodes") == std::string_view::npos);
    assert(shader.find("instances[") == std::string_view::npos);
    assert(shader.find("node.extra.x") != std::string_view::npos);
    assert(shader.find("PHASE_COUNT = 4") != std::string_view::npos);

    const Renderer::PathTracerSettings settings{};
    assert(settings.resolution_divisor == 2);
    assert(settings.samples_per_frame == 1);
    assert(settings.max_bounces == 3);

    return 0;
}
