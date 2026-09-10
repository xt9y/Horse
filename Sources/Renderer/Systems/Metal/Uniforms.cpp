#include "Renderer/Systems/Uniforms.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::Systems {

MetalTraceUniforms makeMetalTraceUniforms(
    const CameraState& camera,
    const LightState& light,
    int trace_width,
    int trace_height,
    int output_width,
    int output_height,
    std::size_t node_count,
    std::size_t triangle_count,
    std::size_t material_count,
    std::uint32_t frame_index,
    bool reset,
    bool camera_moving)
{
    constexpr float pi = 3.14159265358979323846f;
    MetalTraceUniforms uniforms{};
    uniforms.camera_position = {camera.position.x, camera.position.y, camera.position.z, 0.0f};
    uniforms.camera_forward = {camera.forward.x, camera.forward.y, camera.forward.z, 0.0f};
    uniforms.camera_right = {camera.right.x, camera.right.y, camera.right.z, 0.0f};
    uniforms.camera_up = {camera.up.x, camera.up.y, camera.up.z, 0.0f};
    uniforms.light_position_intensity = {
        light.position.x,
        light.position.y,
        light.position.z,
        light.intensity
    };
    uniforms.light_color_tan_half_fov = {
        light.color.x,
        light.color.y,
        light.color.z,
        std::tan(camera.fov_degrees * (pi / 360.0f))
    };
    uniforms.resolution_aspect = {
        static_cast<float>(std::max(trace_width, 1)),
        static_cast<float>(std::max(trace_height, 1)),
        static_cast<float>(std::max(output_width, 1)) / static_cast<float>(std::max(output_height, 1)),
        0.0f
    };
    uniforms.counts = {
        static_cast<std::int32_t>(node_count),
        static_cast<std::int32_t>(triangle_count),
        static_cast<std::int32_t>(material_count),
        0
    };
    const bool point_light = light.valid && light.type == LightType::Point;
    uniforms.frame = {
        frame_index,
        reset ? 1u : 0u,
        point_light ? 1u : 0u,
        camera_moving ? 1u : 0u
    };
    return uniforms;
}

} // namespace Renderer::Systems
