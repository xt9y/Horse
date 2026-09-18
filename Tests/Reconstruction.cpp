#include <Renderer/Reconstruction/Reconstruction.hpp>

#include <cassert>
#include <cstdint>

int main()
{
    using namespace Renderer::Reconstruction;

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
    assert(controller.grid() == 2u);
    for (int i = 0; i < 12; ++i) controller.observe(35.0f, settings);
    assert(controller.grid() > 2u);
    const std::uint32_t degraded = controller.grid();

    for (int i = 0; i < 64; ++i) controller.observe(10.0f, settings);
    assert(controller.grid() < degraded);

    settings.quality = 1.0f;
    controller.reset();
    for (int i = 0; i < 64; ++i) controller.observe(50.0f, settings);
    assert(controller.grid() == 1u);

    controller.reset();
    const float before = controller.averageFrameMs();
    controller.observe(0.0f, settings);
    controller.observe(500.0f, settings);
    assert(controller.averageFrameMs() == before);

    return 0;
}
