#ifndef HORSE_RENDERER_GAUSSIAN_SPLAT_PROJECTION_HPP
#define HORSE_RENDERER_GAUSSIAN_SPLAT_PROJECTION_HPP

#include "Models/GaussianSplat.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Systems/SceneCache.hpp"

#include <cstdint>

namespace Renderer::GaussianSplat {

struct ProjectedSplat {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float axis0_x = 0.0f;
    float axis0_y = 0.0f;
    float axis1_x = 0.0f;
    float axis1_y = 0.0f;
    Vec3 color{};
    float opacity = 1.0f;
    float ndc_depth = 0.0f;
    float linear_depth = 0.0f;
    float distance_squared = 0.0f;
};

bool project(
    const Models::GaussianSplat::Splat& splat,
    std::uint32_t spherical_harmonic_degree,
    const Math::Mat4& model,
    const Systems::CameraState& camera,
    int width,
    int height,
    ProjectedSplat *output
);

} // namespace Renderer::GaussianSplat

#endif
