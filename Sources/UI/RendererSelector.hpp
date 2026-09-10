#ifndef HORSE_UI_RENDERER_SELECTOR_HPP
#define HORSE_UI_RENDERER_SELECTOR_HPP

#include <cstdint>

namespace UI {

enum class RendererChoice : std::uint8_t {
    Rasterizer,
    RayTracer,
    PathTracer,
};

struct RendererAvailability {
    bool rasterizer = false;
    bool ray_tracer = false;
    bool path_tracer = false;
};

bool rendererSelector(
    RendererChoice& choice,
    const RendererAvailability& available
);

const char *rendererName(RendererChoice choice);

} // namespace UI

#endif
