#include "Renderer/SDLGPU/Uniforms.hpp"

#include "Renderer/Trace/CameraProjection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Renderer::SDLGPU {

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
    std::uint32_t frame_index,
    std::uint32_t previous_samples,
    std::uint32_t samples_per_frame,
    bool reset,
    bool camera_moving,
    std::uint32_t stationary_phase_grid,
    std::uint32_t reset_phase_grid,
    std::uint32_t moving_phase_grid,
    std::uint32_t moving_depth_block)
{
    FrameUniforms uniforms{};
    const float viewport_aspect = static_cast<float>(std::max(output_width, 1)) /
        static_cast<float>(std::max(output_height, 1));
    const Trace::CameraProjectionEncoding projection =
        Trace::cameraProjectionEncoding(camera, viewport_aspect);

    uniforms.camera_position_near = {
        camera.position.x, camera.position.y, camera.position.z,
        std::max(camera.near_plane, 1.0e-4f)};
    uniforms.camera_forward_far = {
        camera.forward.x, camera.forward.y, camera.forward.z,
        camera.far_plane > camera.near_plane
            ? camera.far_plane
            : std::numeric_limits<float>::max()};
    uniforms.camera_right_aspect = {
        camera.right.x, camera.right.y, camera.right.z,
        std::max(projection.aspect, 1.0e-6f)};
    uniforms.camera_up_tan_half_fov = {
        camera.up.x, camera.up.y, camera.up.z,
        camera.projection == Camera::Projection::Perspective
            ? std::max(projection.scale, 1.0e-6f)
            : 1.0f};
    uniforms.projection_alpha = {
        std::max(std::abs(camera.xmag), 1.0e-6f),
        std::max(std::abs(camera.ymag), 1.0e-6f),
        camera.projection == Camera::Projection::Orthographic ? 1.0f : 0.0f,
        std::clamp(alpha_cutoff, 0.0f, 1.0f)};
    uniforms.resolution = {
        static_cast<float>(std::max(trace_width, 1)),
        static_cast<float>(std::max(trace_height, 1)),
        static_cast<float>(std::max(output_width, 1)),
        static_cast<float>(std::max(output_height, 1))};
    uniforms.counts = {
        static_cast<std::int32_t>(std::min<std::size_t>(node_count, INT32_MAX)),
        static_cast<std::int32_t>(std::min<std::size_t>(triangle_count, INT32_MAX)),
        static_cast<std::int32_t>(std::min<std::size_t>(material_count, INT32_MAX)),
        static_cast<std::int32_t>(std::min<std::size_t>(texture_count, INT32_MAX))};
    uniforms.frame = {
        frame_index,
        previous_samples,
        std::max(samples_per_frame, 1u),
        (reset ? 1u : 0u) | (camera_moving ? 2u : 0u)};
    uniforms.path_policy = {
        std::max(stationary_phase_grid, 1u),
        std::max(reset_phase_grid, 1u),
        std::max(moving_phase_grid, 1u),
        std::max(moving_depth_block, 1u)};
    return uniforms;
}

} // namespace Renderer::SDLGPU
