#include "Camera.hpp"

#include <cmath>

namespace Camera {

Ecs::Entity activeCamera(const Ecs::World& world)
{
    Ecs::Entity result = Ecs::INVALID_ENTITY;
    world.each<CameraComponent, Renderer::Transform>(
        [&](Ecs::Entity entity, const CameraComponent& camera, const Renderer::Transform&) {
            if (result == Ecs::INVALID_ENTITY && camera.active) result = entity;
        }
    );
    return result;
}

Renderer::Vec3 flightDirection(float yaw_degrees, float pitch_degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    const float yaw = yaw_degrees * (pi / 180.0f);
    const float pitch = pitch_degrees * (pi / 180.0f);
    const float cos_pitch = std::cos(pitch);

    return {
        -std::sin(yaw) * cos_pitch,
        std::sin(pitch),
        -std::cos(yaw) * cos_pitch,
    };
}

Renderer::Vec3 strafeDirection(float yaw_degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    const float yaw = yaw_degrees * (pi / 180.0f);
    return {std::cos(yaw), 0.0f, -std::sin(yaw)};
}

} // namespace Camera
