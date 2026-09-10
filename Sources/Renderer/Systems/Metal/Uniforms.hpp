#ifndef RW_ENGINE_RENDERER_SYSTEMS_METAL_UNIFORMS_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_METAL_UNIFORMS_HPP

#include "Renderer/Scenes/SceneCache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Renderer::Systems {

struct alignas(16) MetalTraceUniforms {
    std::array<float, 4> camera_position{};
    std::array<float, 4> camera_forward{};
    std::array<float, 4> camera_right{};
    std::array<float, 4> camera_up{};
    std::array<float, 4> light_position_intensity{};
    std::array<float, 4> light_color_tan_half_fov{};
    std::array<float, 4> resolution_aspect{};
    std::array<std::int32_t, 4> counts{};
    std::array<std::uint32_t, 4> frame{};
};

struct alignas(16) MetalPresentUniforms {
    std::array<float, 4> exposure{};
};

MetalTraceUniforms makeMetalTraceUniforms(
    const Renderer::Scenes::CameraState& camera,
    const Renderer::Scenes::LightState& light,
    int trace_width,
    int trace_height,
    int output_width,
    int output_height,
    std::size_t node_count,
    std::size_t triangle_count,
    std::size_t material_count,
    float alpha_cutoff,
    std::uint32_t frame_index,
    bool reset,
    bool camera_moving
);

static_assert(sizeof(MetalTraceUniforms) == 144u);
static_assert(sizeof(MetalPresentUniforms) == 16u);

} // namespace Renderer::Systems

#endif
