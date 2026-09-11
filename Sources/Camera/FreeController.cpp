#include "Camera/FreeController.hpp"

#include "Camera.hpp"
#include "Renderer/Components.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>

namespace Camera {

void FreeController::update(Ecs::World& world, float delta_seconds)
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

    bool changed = false;
    const bool mouse_grabbed = Mouse.isGrabbed() != LWCGL_FALSE;
    const int mouse_dx = Mouse.getDX();
    const int mouse_dy = Mouse.getDY();

    if (mouse_grabbed && (mouse_dx != 0 || mouse_dy != 0)) {
        transform->rotation.y -= static_cast<float>(mouse_dx) * mouse_sensitivity_;
        transform->rotation.x += static_cast<float>(mouse_dy) * mouse_sensitivity_;
        const float minimum_pitch = std::min(minimum_pitch_, maximum_pitch_);
        const float maximum_pitch = std::max(minimum_pitch_, maximum_pitch_);
        transform->rotation.x = std::clamp(transform->rotation.x, minimum_pitch, maximum_pitch);
        changed = true;
    }

    const Renderer::Vec3 forward = flightDirection(
        transform->rotation.y,
        transform->rotation.x
    );
    const Renderer::Vec3 right = strafeDirection(transform->rotation.y);

    float speed = speed_ * std::max(delta_seconds, 0.0f);
    if (Keyboard.isKeyDown(Keyboard.KEY_LSHIFT)) speed *= sprint_multiplier_;

    auto move = [&](const Renderer::Vec3& direction, float amount) {
        if (amount == 0.0f) return;
        transform->position.x += direction.x * amount;
        transform->position.y += direction.y * amount;
        transform->position.z += direction.z * amount;
        changed = true;
    };

    if (Keyboard.isKeyDown(Keyboard.KEY_W)) move(forward, speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_S)) move(forward, -speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_D)) move(right, speed);
    if (Keyboard.isKeyDown(Keyboard.KEY_A)) move(right, -speed);

    if (changed) world.markChanged(Ecs::ChangeKind::Camera);
}

} // namespace Camera
