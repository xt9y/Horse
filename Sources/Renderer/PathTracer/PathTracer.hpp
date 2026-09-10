#ifndef RW_ENGINE_RENDERER_PATHTRACER_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer {

struct PathTracerSettings {
    bool enabled = false;
    int resolution_divisor = 0;
    int samples_per_frame = 0;
    int max_bounces = 0;
    float exposure = 0.0f;
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

    void setResolutionDivisor(int divisor) { settings().resolution_divisor = divisor; }
    void setSamplesPerFrame(int samples) { settings().samples_per_frame = samples; }
    void setExposure(float exposure) { settings().exposure = exposure; }
    int resolutionDivisor() const { return settings().resolution_divisor; }
    int samplesPerFrame() const { return settings().samples_per_frame; }
    float exposure() const { return settings().exposure; }

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
