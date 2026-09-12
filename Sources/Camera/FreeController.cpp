#include "Camera/FreeController.hpp"

#include "Camera.hpp"
#include "Input.hpp"
#include "Renderer/Components.hpp"

#include <algorithm>

namespace Camera {

void FreeController::update(Ecs::World& world, float delta_seconds)
{
    const Ecs::Entity camera_entity = activeCamera(world);
    if (camera_entity == Ecs::INVALID_ENTITY) return;

    Renderer::Transform *transform = world.get<Renderer::Transform>(camera_entity);
    if (!transform) return;

    if (!mouse_initialized_) {
        Input::setPointerCaptured(true);
        mouse_initialized_ = true;
    }

    bool changed = false;
    const Input::Pointer pointer = Input::pointer();
    const bool mouse_grabbed = pointer.captured;
    const int mouse_dx = pointer.dx;
    const int mouse_dy = pointer.dy;

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
    if (Input::keyDown(Input::key("Left Shift"))) speed *= sprint_multiplier_;

    auto move = [&](const Renderer::Vec3& direction, float amount) {
        if (amount == 0.0f) return;
        transform->position.x += direction.x * amount;
        transform->position.y += direction.y * amount;
        transform->position.z += direction.z * amount;
        changed = true;
    };

    if (Input::keyDown(Input::key("W"))) move(forward, speed);
    if (Input::keyDown(Input::key("S"))) move(forward, -speed);
    if (Input::keyDown(Input::key("D"))) move(right, speed);
    if (Input::keyDown(Input::key("A"))) move(right, -speed);

    if (changed) world.markChanged(Ecs::ChangeKind::Camera);
}

} // namespace Camera
