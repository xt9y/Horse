#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

#include "Camera/FreeController.hpp"
#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

namespace Camera {

struct CameraComponent {
    float fov_degrees = 0.0f;
    float near_plane = 0.0f;
    bool active = false;
};

Ecs::Entity activeCamera(const Ecs::World& world);
Renderer::Vec3 flightDirection(float yaw_degrees, float pitch_degrees);
Renderer::Vec3 strafeDirection(float yaw_degrees);

} // namespace Camera

#endif
