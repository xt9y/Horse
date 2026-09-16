#ifndef HORSE_RENDERER_RASTERIZER_HPP
#define HORSE_RENDERER_RASTERIZER_HPP

#include "Renderer/Components.hpp"
#include "Renderer/Renderer.hpp"

namespace Renderer {

struct RasterizerSettings {
    bool enabled = true;
    bool viewport_culling = true;
    int shadow_resolution = 512;
    int shadow_cascades = 4;
    float shadow_distance = 80.0f;
    float shadow_near_plane = 0.05f;
    Vec4 clear_color {0.0f, 0.0f, 0.0f, 1.0f};
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
    void setShadowCascades(int value);
    void setShadowDistance(float value);
    void setShadowNearPlane(float value);
    void setClearColor(Vec4 value);

    bool viewportCulling() const;
    int shadowResolution() const;
    int shadowCascades() const;
    float shadowDistance() const;
    float shadowNearPlane() const;
    Vec4 clearColor() const;

    RasterizerSettings& settings();
    const RasterizerSettings& settings() const;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    bool compose(Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl* impl_;
};

} // namespace Renderer

#endif
