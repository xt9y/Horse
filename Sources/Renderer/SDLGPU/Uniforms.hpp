#ifndef HORSE_RENDERER_SDLGPU_UNIFORMS_HPP
#define HORSE_RENDERER_SDLGPU_UNIFORMS_HPP

#include "Renderer/Scenes/SceneCache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Renderer::SDLGPU {

struct alignas(16) FrameUniforms {
    std::array<float, 4> camera_position_near{};
    std::array<float, 4> camera_forward_far{};
    std::array<float, 4> camera_right_aspect{};
    std::array<float, 4> camera_up_tan_half_fov{};
    std::array<float, 4> projection_alpha{};
    std::array<float, 4> resolution{};
    std::array<std::int32_t, 4> counts{};
    std::array<std::uint32_t, 4> frame{};
    std::array<std::uint32_t, 4> path_policy{};
};

FrameUniforms makeFrameUniforms(
    const Scenes::CameraState& camera,
    int trace_width,
    int trace_height,
    int output_width,
    int output_height,
    std::size_t node_count,
    std::size_t triangle_count,
    std::size_t material_count,
    std::size_t texture_count,
    float alpha_cutoff,
    std::uint32_t frame_index = 0u,
    std::uint32_t previous_samples = 0u,
    std::uint32_t samples_per_frame = 1u,
    bool reset = false,
    bool camera_moving = false,
    std::uint32_t stationary_phase_grid = 1u,
    std::uint32_t reset_phase_grid = 1u,
    std::uint32_t moving_phase_grid = 1u,
    std::uint32_t moving_depth_block = 1u
);

static_assert(sizeof(FrameUniforms) == 144u);

} // namespace Renderer::SDLGPU

#endif
