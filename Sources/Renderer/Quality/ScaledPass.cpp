#include "Renderer/Quality/ScaledPass.hpp"

#include <algorithm>

namespace Renderer::Quality {

ScaledPassSettings sanitized(ScaledPassSettings settings)
{
    settings.resolution_divisor = std::max(settings.resolution_divisor, 1);
    settings.temporal_weight = std::clamp(settings.temporal_weight, 0.0f, 1.0f);
    settings.depth_threshold = std::max(settings.depth_threshold, 0.0f);
    return settings;
}

Extent scaledExtent(int width, int height, const ScaledPassSettings& settings)
{
    const ScaledPassSettings value = sanitized(settings);
    const int safe_width = std::max(width, 1);
    const int safe_height = std::max(height, 1);
    return {
        std::max((safe_width + value.resolution_divisor - 1) / value.resolution_divisor, 1),
        std::max((safe_height + value.resolution_divisor - 1) / value.resolution_divisor, 1),
    };
}

} // namespace Renderer::Quality
