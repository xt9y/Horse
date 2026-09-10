#include "Renderer/PathTracer/PathTracer.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstdint>

namespace Renderer {
namespace {

std::uint64_t phaseBudget(int width, int height, int grid)
{
    const std::uint64_t safe_width = static_cast<std::uint64_t>(std::max(width, 1));
    const std::uint64_t safe_height = static_cast<std::uint64_t>(std::max(height, 1));
    const std::uint64_t safe_grid = static_cast<std::uint64_t>(std::max(grid, 1));
    const std::uint64_t columns = (safe_width + safe_grid - 1u) / safe_grid;
    const std::uint64_t rows = (safe_height + safe_grid - 1u) / safe_grid;
    return columns * rows;
}

} // namespace

PathTracerStatistics PathTracer::statistics() const
{
    const PathTracerSettings& value = settings();
    PathTracerStatistics result{};
    result.active = initialized() && enabled();
    result.output_width = std::max(Display.getWidth(), 1);
    result.output_height = std::max(Display.getHeight(), 1);

    const int divisor = std::max(value.resolution_divisor, 1);
    result.trace_width = std::max(result.output_width / divisor, 1);
    result.trace_height = std::max(result.output_height / divisor, 1);
    result.samples_per_frame = value.samples_per_frame;
    result.stationary_phase_grid = value.stationary_phase_grid;
    result.reset_phase_grid = value.reset_phase_grid;
    result.moving_phase_grid = value.moving_phase_grid;
    result.moving_depth_block = value.moving_depth_block;
    result.stationary_path_pixel_budget = phaseBudget(
        result.trace_width,
        result.trace_height,
        value.stationary_phase_grid
    );
    result.reset_path_pixel_budget = phaseBudget(
        result.trace_width,
        result.trace_height,
        value.reset_phase_grid
    );
    result.moving_path_pixel_budget = phaseBudget(
        result.trace_width,
        result.trace_height,
        value.moving_phase_grid
    );
    result.moving_depth_ray_budget = phaseBudget(
        result.trace_width,
        result.trace_height,
        value.moving_depth_block
    );
    return result;
}

} // namespace Renderer
