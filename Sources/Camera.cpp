#include "Camera.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
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

void Controller::update(Ecs::World& world, float delta_seconds)
{
    const Ecs::Entity camera_entity = activeCamera(world);
    if (camera_entity == Ecs::INVALID_ENTITY) return;

    Renderer::Transform *transform = world.get<Renderer::Transform>(camera_entity);
    if (!transform) return;

    if (!mouse_initialized_) {
        Mouse.setGrabbed(LWCGL_TRUE);
        Mouse.getDX();
        Mouse.getDY();
        mouse_initialized_ = true;
    }

    const bool mouse_grabbed = Mouse.isGrabbed() != LWCGL_FALSE;
    const int mouse_dx = Mouse.getDX();
    const int mouse_dy = Mouse.getDY();

    if (mouse_grabbed) {
        constexpr float mouse_sensitivity = 0.12f;
        transform->rotation.y -= static_cast<float>(mouse_dx) * mouse_sensitivity;
        transform->rotation.x += static_cast<float>(mouse_dy) * mouse_sensitivity;
        transform->rotation.x = std::clamp(transform->rotation.x, -89.0f, 89.0f);
    }

    const Renderer::Vec3 forward = flightDirection(
        transform->rotation.y,
        transform->rotation.x
    );
    const Renderer::Vec3 right = strafeDirection(transform->rotation.y);

    float speed = 100.0f * delta_seconds;
    if (Keyboard.isKeyDown(Keyboard.KEY_LSHIFT)) speed *= 10.0f;

    auto move = [&](const Renderer::Vec3& direction, float amount) {
        transform->position.x += direction.x * amount;
        transform->position.y += direction.y * amount;
        transform->position.z += direction.z * amount;
    };

    if (Keyboard.isKeyDown(Keyboard.KEY_W)) move(forward, speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_S)) move(forward, -speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_D)) move(right, speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_A)) move(right, -speed);
}

} // namespace Camera
