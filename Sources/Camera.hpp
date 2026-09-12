#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

#include "Camera/FreeController.hpp"
#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

namespace Camera {

enum class Projection {
    Perspective,
    Orthographic,
};

struct CameraComponent {
    float fov_degrees = 0.0f;
    float near_plane = 0.0f;
    bool active = false;
    Projection projection = Projection::Perspective;
    float far_plane = 0.0f;
    float aspect_ratio = 0.0f;
    float xmag = 1.0f;
    float ymag = 1.0f;
};

Ecs::Entity activeCamera(const Ecs::World& world);
Renderer::Vec3 flightDirection(float yaw_degrees, float pitch_degrees);
Renderer::Vec3 strafeDirection(float yaw_degrees);

} // namespace Camera

#endif
