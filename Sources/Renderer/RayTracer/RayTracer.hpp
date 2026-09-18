#ifndef HORSE_RENDERER_RAYTRACER_RAYTRACER_HPP
#define HORSE_RENDERER_RAYTRACER_RAYTRACER_HPP

#include "Renderer/Reconstruction/Reconstruction.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer {

struct RayTracerSettings {
    bool enabled = false;
    int resolution_divisor = 0;
    Reconstruction::Settings reconstruction{};
};

class RayTracer final : public IRenderer {
public:
    RayTracer();
    ~RayTracer() override;

    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    bool init() override;
    bool activate() override;
    void deactivate() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

    void setResolutionDivisor(int divisor) { settings().resolution_divisor = divisor; }
    int resolutionDivisor() const { return settings().resolution_divisor; }

    Reconstruction::Settings& reconstructionSettings() { return settings().reconstruction; }
    const Reconstruction::Settings& reconstructionSettings() const { return settings().reconstruction; }

    RayTracerSettings& settings();
    const RayTracerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    bool compose(Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer

#endif
