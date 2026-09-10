#ifndef RW_ENGINE_RENDERER_RASTERIZER_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_HPP

#include "Renderer/Components.hpp"
#include "Renderer/HorizonGI/HorizonGI.hpp"
#include "Renderer/Quality/ScaledPass.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Upscale/Upscale.hpp"

#include <cstddef>

namespace Renderer {

struct RasterizerSettings {
    bool enabled = false;
    bool viewport_culling = false;
    int shadow_resolution = 0;
    int shadow_resolution_divisor = 1;
    int fallback_shadow_resolution = 0;
    int minimum_shadow_resolution = 0;
    float shadow_near_plane = 0.0f;
    float shadow_far_scale = 0.0f;
    Quality::ScaledPassSettings lighting{};
    HorizonGI::Settings horizon_gi{};
    Vec4 clear_color{};
};

struct RasterizerStatistics {
    bool scaled_pipeline_active = false;
    bool shadow_active = false;
    int output_width = 0;
    int output_height = 0;
    int lighting_width = 0;
    int lighting_height = 0;
    int shadow_resolution = 0;
    std::size_t depth_prepass_items = 0u;
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
    void setShadowResolutionDivisor(int value);
    void setFallbackShadowResolution(int value);
    void setMinimumShadowResolution(int value);
    void setShadowNearPlane(float value);
    void setShadowFarScale(float value);
    void setLightingResolutionDivisor(int value);
    void setDepthAwareUpscaling(bool value);
    void setTemporalUpscaling(bool value);
    void setTemporalUpscalingWeight(float value);
    void setUpscalingDepthThreshold(float value);
    void setHorizonGiEnabled(bool value);
    void setHorizonGiResolutionDivisor(int value);
    void setHorizonGiDirections(int value);
    void setHorizonGiSteps(int value);
    void setHorizonGiRadius(float value);
    void setHorizonGiThickness(float value);
    void setHorizonGiAoStrength(float value);
    void setHorizonGiIndirectStrength(float value);
    void setHorizonGiTemporalFilter(bool value);
    void setHorizonGiTemporalWeight(float value);
    void setClearColor(Vec4 value);

    bool viewportCulling() const;
    int shadowResolution() const;
    int shadowResolutionDivisor() const;
    int fallbackShadowResolution() const;
    int minimumShadowResolution() const;
    float shadowNearPlane() const;
    float shadowFarScale() const;
    int lightingResolutionDivisor() const;
    bool depthAwareUpscaling() const;
    bool temporalUpscaling() const;
    float temporalUpscalingWeight() const;
    float upscalingDepthThreshold() const;
    HorizonGI::Settings& horizonGiSettings();
    const HorizonGI::Settings& horizonGiSettings() const;
    HorizonGI::Statistics horizonGiStatistics() const;
    Upscale::Statistics upscaleStatistics() const;
    RasterizerStatistics statistics() const;
    Vec4 clearColor() const;

    RasterizerSettings& settings();
    const RasterizerSettings& settings() const;

protected:
    bool usesGlobalIlluminationField(const Ecs::World& world) const override;
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl* impl_;
};

} // namespace Renderer

#endif
