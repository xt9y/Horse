#ifndef HORSE_RENDERER_RECONSTRUCTION_RECONSTRUCTION_HPP
#define HORSE_RENDERER_RECONSTRUCTION_RECONSTRUCTION_HPP

#include <cstdint>

namespace Renderer::Reconstruction {

struct Settings {
    float quality = 0.50f;
    std::uint32_t maximum_history = 32u;
    bool temporal_reuse = true;
    bool debug_reconstruction = false;
    float target_frame_ms = 16.67f;
};

std::uint32_t phaseIndex(
    std::uint32_t frame_index,
    std::uint32_t grid);

bool scheduledPixel(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t frame_index,
    std::uint32_t grid);

class BudgetController {
public:
    BudgetController();

    void reset();
    void observe(float frame_ms, const Settings& settings);

    std::uint32_t grid() const { return grid_; }
    float averageFrameMs() const { return average_frame_ms_; }

private:
    std::uint32_t grid_ = 2u;
    float average_frame_ms_ = 0.0f;
    std::uint32_t over_budget_frames_ = 0u;
    std::uint32_t under_budget_frames_ = 0u;
    bool has_sample_ = false;
};

} // namespace Renderer::Reconstruction

#endif
