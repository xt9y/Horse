#ifndef RW_ENGINE_RENDERER_RAYTRACER_RAYTRACER_HPP
#define RW_ENGINE_RENDERER_RAYTRACER_RAYTRACER_HPP

#include "Renderer/FontPass.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer {

struct RayTracerSettings {
    bool enabled = true;
    int resolution_divisor = 4;
    float exposure = 1.0f;
};

class RayTracer final : public IRenderer {
public:
    RayTracer();
    ~RayTracer() override;

    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    bool init() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

    RayTracerSettings& settings();
    const RayTracerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer

#endif
