#ifndef HORSE_CAMERA_HPP
#define HORSE_CAMERA_HPP

#include "Camera/FreeController.hpp"
#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

#include <cstdint>

namespace Camera {

enum class Projection {
    Perspective,
    Orthographic,
};

enum class ToneMapping : std::uint8_t {
    None,
    ACES,
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
    float exposure_ev = 0.0f;
    ToneMapping tone_mapping = ToneMapping::ACES;
};

Ecs::Entity activeCamera(const Ecs::World& world);
Renderer::Vec3 flightDirection(float yaw_degrees, float pitch_degrees);
Renderer::Vec3 strafeDirection(float yaw_degrees);

} // namespace Camera

#endif
