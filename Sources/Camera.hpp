#ifndef RW_ENGINE_CAMERA_HPP
#define RW_ENGINE_CAMERA_HPP

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

class Controller {
public:
    void setSpeed(float value) { speed_ = value; }
    void setSprintMultiplier(float value) { sprint_multiplier_ = value; }
    void setMouseSensitivity(float value) { mouse_sensitivity_ = value; }
    void setPitchRange(float minimum, float maximum)
    {
        minimum_pitch_ = minimum;
        maximum_pitch_ = maximum;
    }

    float speed() const { return speed_; }
    float sprintMultiplier() const { return sprint_multiplier_; }
    float mouseSensitivity() const { return mouse_sensitivity_; }
    float minimumPitch() const { return minimum_pitch_; }
    float maximumPitch() const { return maximum_pitch_; }

    void update(Ecs::World& world, float delta_seconds);

private:
    float speed_ = 0.0f;
    float sprint_multiplier_ = 0.0f;
    float mouse_sensitivity_ = 0.0f;
    float minimum_pitch_ = 0.0f;
    float maximum_pitch_ = 0.0f;
    bool mouse_initialized_ = false;
};

} // namespace Camera

#endif
