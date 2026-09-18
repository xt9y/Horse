#include <Renderer/Internal/ReconstructionSDLGPU.hpp>
#include <Renderer/Reconstruction/Reconstruction.hpp>
#include <Renderer/SDLGPU/ReconstructionShaders.hpp>

#include <cassert>
#include <cstdint>
#include <string_view>
#include <type_traits>

int main()
{
    using namespace Renderer::Reconstruction;

    static_assert(std::is_default_constructible_v<Renderer::Internal::ReconstructionSDLGPU>);
    static_assert(!std::is_copy_constructible_v<Renderer::Internal::ReconstructionSDLGPU>);

    assert(phaseIndex(0u, 2u) == 0u);
    assert(phaseIndex(3u, 2u) == 3u);
    assert(phaseIndex(4u, 2u) == 0u);

    for (std::uint32_t y = 0u; y < 8u; ++y) {
        for (std::uint32_t x = 0u; x < 8u; ++x) {
            bool visited = false;
            for (std::uint32_t frame = 0u; frame < 16u; ++frame)
                visited |= scheduledPixel(x, y, frame, 4u);
            assert(visited);
        }
    }

    Settings settings;
    assert(settings.quality == 0.50f);
    assert(settings.maximum_history == 32u);
    assert(settings.temporal_reuse);
    assert(!settings.debug_reconstruction);
    assert(settings.target_frame_ms == 16.67f);

    settings.quality = 0.0f;
    BudgetController controller;
    assert(controller.grid() == 1u);
    for (int i = 0; i < 12; ++i) controller.observe(35.0f, settings);
    assert(controller.grid() > 1u);
    const std::uint32_t degraded = controller.grid();

    for (int i = 0; i < 64; ++i) controller.observe(10.0f, settings);
    assert(controller.grid() < degraded);

    settings.quality = 1.0f;
    controller.reset();
    assert(controller.grid() == 1u);
    for (int i = 0; i < 64; ++i) controller.observe(50.0f, settings);
    assert(controller.grid() == 1u);

    controller.reset();
    const float before = controller.averageFrameMs();
    controller.observe(0.0f, settings);
    controller.observe(500.0f, settings);
    assert(controller.averageFrameMs() == before);

    const std::string_view resolve = Renderer::SDLGPU::ReconstructionShaders::Resolve;
    const std::size_t history_write = resolve.find("NextColor[pixel] = color;");
    const std::size_t debug_branch = resolve.find("if (Control.w != 0u)");
    assert(history_write != std::string_view::npos);
    assert(debug_branch != std::string_view::npos);
    assert(history_write < debug_branch);
    assert(resolve.find("if (depth <= 0.0 && expected_depth > 0.0) return 0.0;") !=
        std::string_view::npos);

    return 0;
}
