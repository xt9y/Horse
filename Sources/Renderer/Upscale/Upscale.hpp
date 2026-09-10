#ifndef HORSE_RENDERER_UPSCALE_HPP
#define HORSE_RENDERER_UPSCALE_HPP

namespace Renderer::Upscale {

struct Statistics {
    bool active = false;
    bool depth_aware = false;
    bool temporal_history = false;
    bool effect = false;
    int source_width = 0;
    int source_height = 0;
    int output_width = 0;
    int output_height = 0;
};

} // namespace Renderer::Upscale

#endif
