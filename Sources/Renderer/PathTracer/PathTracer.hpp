#ifndef RW_ENGINE_RENDERER_PATHTRACER_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer {

struct PathTracerSettings {
    bool enabled = true;
    int resolution_divisor = 4;
    int samples_per_frame = 1;
    float exposure = 1.0f;
};

class PathTracer final : public IRenderer {
public:
    struct Impl;

    PathTracer();
    ~PathTracer() override;

    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    bool init() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

    PathTracerSettings& settings();
    const PathTracerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl *impl_;
};

} // namespace Renderer

#endif
