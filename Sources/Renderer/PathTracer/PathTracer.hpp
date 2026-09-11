#ifndef RW_ENGINE_RENDERER_PATHTRACER_HPP
#define RW_ENGINE_RENDERER_PATHTRACER_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer {

struct PathTracerSettings {
    bool enabled = false;
    int resolution_divisor = 0;
    int samples_per_frame = 0;
    int stationary_phase_grid = 0;
    int reset_phase_grid = 0;
    int moving_phase_grid = 0;
    int moving_depth_block = 0;
};

class PathTracer final : public IRenderer {
public:
    struct Impl;

    PathTracer();
    ~PathTracer() override;

    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    bool init() override;
    bool activate() override;
    void deactivate() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

    void setResolutionDivisor(int divisor) { settings().resolution_divisor = divisor; }
    void setSamplesPerFrame(int samples) { settings().samples_per_frame = samples; }
    void setStationaryPhaseGrid(int value) { settings().stationary_phase_grid = value; }
    void setResetPhaseGrid(int value) { settings().reset_phase_grid = value; }
    void setMovingPhaseGrid(int value) { settings().moving_phase_grid = value; }
    void setMovingDepthBlock(int value) { settings().moving_depth_block = value; }
    int resolutionDivisor() const { return settings().resolution_divisor; }
    int samplesPerFrame() const { return settings().samples_per_frame; }
    int stationaryPhaseGrid() const { return settings().stationary_phase_grid; }
    int resetPhaseGrid() const { return settings().reset_phase_grid; }
    int movingPhaseGrid() const { return settings().moving_phase_grid; }
    int movingDepthBlock() const { return settings().moving_depth_block; }

    PathTracerSettings& settings();
    const PathTracerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    bool compose(Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl *impl_;
};

} // namespace Renderer

#endif
