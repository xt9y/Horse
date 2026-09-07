#ifndef RW_ENGINE_RENDERER_PATHTRACER_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_HPP

#include "Ecs/Ecs.hpp"

// PathTracer.cpp uses the standard OpenGL texture API. Preload lwcgl's GL11
// compatibility declarations without its LWJGL-style function macros so the
// later compatibility include cannot replace glGenTextures/glDeleteTextures.
#ifndef LWCGL_IMPLEMENTATION
#define RW_PATHTRACER_DEFINED_LWCGL_IMPLEMENTATION
#define LWCGL_IMPLEMENTATION
#endif
#include <lwcgl/gl11_compat.h>
#ifdef RW_PATHTRACER_DEFINED_LWCGL_IMPLEMENTATION
#undef LWCGL_IMPLEMENTATION
#undef RW_PATHTRACER_DEFINED_LWCGL_IMPLEMENTATION
#endif

namespace Renderer {

struct PathTracerSettings {
    bool enabled = true;
    int resolution_divisor = 2;
    int samples_per_frame = 1;
    int max_bounces = 3;
    float exposure = 1.0f;
};

class PathTracer {
public:
    struct Impl;

    PathTracer();
    ~PathTracer();

    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    bool init();
    void resize(int width, int height);
    void render(const Ecs::World& world);
    void shutdown();

    bool initialized() const;
    bool enabled() const;
    void setEnabled(bool enabled);

    PathTracerSettings& settings();
    const PathTracerSettings& settings() const;

private:
    Impl *impl_;
};

} // namespace Renderer

#endif
