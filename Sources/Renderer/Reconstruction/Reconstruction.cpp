#include "Renderer/Reconstruction/Reconstruction.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::Reconstruction {
namespace {

std::uint32_t maximumGrid(float quality)
{
    quality = std::clamp(quality, 0.0f, 1.0f);
    const float scaled = (1.0f - quality) * 7.0f;
    return std::clamp<std::uint32_t>(
        1u + static_cast<std::uint32_t>(std::lround(scaled)),
        1u,
        8u);
}

} // namespace

std::uint32_t phaseIndex(
    std::uint32_t frame_index,
    std::uint32_t grid)
{
    grid = std::max(grid, 1u);
    return frame_index % (grid * grid);
}

bool scheduledPixel(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t frame_index,
    std::uint32_t grid)
{
    grid = std::max(grid, 1u);
    const std::uint32_t phase = phaseIndex(frame_index, grid);
    return (x % grid) + (y % grid) * grid == phase;
}

BudgetController::BudgetController()
{
    reset();
}

void BudgetController::reset()
{
    grid_ = 1u;
    average_frame_ms_ = 0.0f;
    over_budget_frames_ = 0u;
    under_budget_frames_ = 0u;
    has_sample_ = false;
}

void BudgetController::observe(float frame_ms, const Settings& settings)
{
    if (!(frame_ms > 0.0f) || frame_ms > 250.0f) return;

    const std::uint32_t maximum_grid = maximumGrid(settings.quality);
    grid_ = std::clamp(grid_, 1u, maximum_grid);

    if (!has_sample_) {
        average_frame_ms_ = frame_ms;
        has_sample_ = true;
    } else {
        average_frame_ms_ = average_frame_ms_ * 0.90f + frame_ms * 0.10f;
    }

    const float target = std::max(settings.target_frame_ms, 1.0f);
    const float over_threshold = target * 1.10f;
    const float under_threshold = target * 1.02f;

    if (average_frame_ms_ > over_threshold) {
        ++over_budget_frames_;
        under_budget_frames_ = 0u;
        if (over_budget_frames_ >= 3u && grid_ < maximum_grid) {
            ++grid_;
            over_budget_frames_ = 0u;
        }
        return;
    }

    if (average_frame_ms_ < under_threshold) {
        ++under_budget_frames_;
        over_budget_frames_ = 0u;
        if (under_budget_frames_ >= 12u && grid_ > 1u) {
            --grid_;
            under_budget_frames_ = 0u;
        }
        return;
    }

    over_budget_frames_ = 0u;
    under_budget_frames_ = 0u;
}

} // namespace Renderer::Reconstruction
