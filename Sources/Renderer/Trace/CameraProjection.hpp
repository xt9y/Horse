#ifndef HORSE_RENDERER_TRACE_CAMERA_PROJECTION_HPP
#define HORSE_RENDERER_TRACE_CAMERA_PROJECTION_HPP

#include "Renderer/Scenes/SceneCache.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::Trace {

struct CameraProjectionEncoding {
    float scale = 1.0f;
    float aspect = 1.0f;
};

inline CameraProjectionEncoding cameraProjectionEncoding(
    const Scenes::CameraState& camera,
    float viewport_aspect)
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float epsilon = 1.0e-6f;

    if (camera.projection == Camera::Projection::Orthographic) {
        const float xmag = std::max(std::abs(camera.xmag), epsilon);
        const float ymag = std::max(std::abs(camera.ymag), epsilon);
        return {-ymag, xmag / ymag};
    }

    const float aspect = camera.aspect_ratio > epsilon
        ? camera.aspect_ratio
        : std::max(viewport_aspect, epsilon);
    const float fov = std::clamp(camera.fov_degrees, 1.0f, 179.0f);
    return {std::tan(fov * (pi / 360.0f)), aspect};
}

} // namespace Renderer::Trace

#endif
