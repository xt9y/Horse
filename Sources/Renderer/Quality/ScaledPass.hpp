#ifndef HORSE_RENDERER_QUALITY_SCALED_PASS_HPP
#define HORSE_RENDERER_QUALITY_SCALED_PASS_HPP

namespace Renderer::Quality {

struct Extent {
    int width = 1;
    int height = 1;
};

struct ScaledPassSettings {
    int resolution_divisor = 1;
    bool depth_aware_upscale = true;
    bool temporal_filter = false;
    float temporal_weight = 0.85f;
    float depth_threshold = 0.02f;
};

ScaledPassSettings sanitized(ScaledPassSettings settings);
Extent scaledExtent(int width, int height, const ScaledPassSettings& settings);

} // namespace Renderer::Quality

#endif
