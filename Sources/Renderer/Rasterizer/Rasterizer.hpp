#ifndef RW_ENGINE_RENDERER_RASTERIZER_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer {

struct RasterizerSettings {
    bool enabled = false;
    bool viewport_culling = false;
    int shadow_resolution = 0;
    int fallback_shadow_resolution = 0;
    int minimum_shadow_resolution = 0;
    float shadow_near_plane = 0.0f;
    float shadow_far_scale = 0.0f;
    Vec4 clear_color{};
};

class Rasterizer final : public IRenderer {
public:
    struct Impl;

    Rasterizer();
    ~Rasterizer() override;

    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    bool init() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

    void setViewportCulling(bool value);
    void setShadowResolution(int value);
    void setFallbackShadowResolution(int value);
    void setMinimumShadowResolution(int value);
    void setShadowNearPlane(float value);
    void setShadowFarScale(float value);
    void setClearColor(Vec4 value);

    bool viewportCulling() const;
    int shadowResolution() const;
    int fallbackShadowResolution() const;
    int minimumShadowResolution() const;
    float shadowNearPlane() const;
    float shadowFarScale() const;
    Vec4 clearColor() const;

    RasterizerSettings& settings();
    const RasterizerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl* impl_;
};

} // namespace Renderer

#endif
