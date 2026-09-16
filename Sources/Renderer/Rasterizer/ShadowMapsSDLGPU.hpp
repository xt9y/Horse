#ifndef HORSE_RENDERER_RASTERIZER_SHADOW_MAPS_SDLGPU_HPP
#define HORSE_RENDERER_RASTERIZER_SHADOW_MAPS_SDLGPU_HPP

#include "Renderer/Lighting/Lighting.hpp"
#include "Renderer/Rasterizer/RasterGeometrySDLGPU.hpp"
#include "Renderer/Scenes/SceneResourcesSDLGPU.hpp"

#include <SDL3/SDL_gpu.h>

#include <string>

namespace Renderer::RasterizerSDLGPU {

struct ShadowMapSettings {
    int resolution = 512;
    int cascades = 4;
    float distance = 80.0f;
    float near_plane = 0.05f;
};

class ShadowMaps {
public:
    ShadowMaps() = default;
    ~ShadowMaps();

    ShadowMaps(const ShadowMaps&) = delete;
    ShadowMaps& operator=(const ShadowMaps&) = delete;

    bool init(std::string *error = nullptr);
    bool update(
        SDL_GPUCommandBuffer *command,
        const RasterGeometry& geometry,
        const Scenes::CameraState& camera,
        const Lighting::State& lighting,
        int viewport_width,
        int viewport_height,
        const ShadowMapSettings& settings,
        std::string *error = nullptr
    );
    void bind(SDL_GPURenderPass *pass) const;
    void clear();

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::RasterizerSDLGPU

#endif
