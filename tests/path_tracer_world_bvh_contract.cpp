#include "Sources/Renderer/PathTracer/PathTracer.hpp"
#include "Sources/Renderer/PathTracer/PathTracerShaders.hpp"

#include <cassert>
#include <string_view>

int main()
{
    const std::string_view shader(Renderer::PathTracerShaders::trace);
    const std::string_view present(Renderer::PathTracerShaders::present_fragment);

    assert(shader.find("uint stack[") == std::string_view::npos);
    assert(shader.find("tlas_nodes") == std::string_view::npos);
    assert(shader.find("instances[") == std::string_view::npos);
    assert(shader.find("node.extra.x") != std::string_view::npos);
    assert(shader.find("STATIONARY_PHASE_GRID = 2") != std::string_view::npos);
    assert(shader.find("MOVING_PHASE_GRID = 4") != std::string_view::npos);
    assert(shader.find("uResetAccumulation != 0 ? MOVING_PHASE_GRID : STATIONARY_PHASE_GRID") != std::string_view::npos);
    assert(shader.find("uniform int uFrameIndex;") != std::string_view::npos);
    assert(shader.find("uniform int uResetAccumulation;") != std::string_view::npos);
    assert(shader.find("pixel_phase != phase") != std::string_view::npos);

    const std::size_t clear_history = shader.find(
        "imageStore(uAccumulation, pixel, vec4(0.0));"
    );
    const std::size_t phase_reject = shader.find("if (pixel_phase != phase) return;");
    assert(clear_history != std::string_view::npos);
    assert(phase_reject != std::string_view::npos);
    assert(clear_history < phase_reject);

    assert(present.find("vec4 sample =") == std::string_view::npos);
    assert(present.find("vec4 candidate_sample =") != std::string_view::npos);
    assert(present.find("candidate_sample.a <= 0.0") != std::string_view::npos);
    assert(present.find("exact.a > 0.0") != std::string_view::npos);
    assert(present.find("blockComplete") != std::string_view::npos);

    const Renderer::PathTracerSettings settings{};
    assert(settings.resolution_divisor == 4);
    assert(settings.samples_per_frame == 1);
    assert(settings.max_bounces == 3);

    return 0;
}
