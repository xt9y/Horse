#ifndef HORSE_CAMERA_FREE_CONTROLLER_HPP
#define HORSE_CAMERA_FREE_CONTROLLER_HPP

#include "Ecs/Ecs.hpp"

namespace Camera {

class FreeController {
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

using Controller = FreeController;

} // namespace Camera

#endif
