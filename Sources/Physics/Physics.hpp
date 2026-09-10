#ifndef HORSE_PHYSICS_PHYSICS_HPP
#define HORSE_PHYSICS_PHYSICS_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

#include <vector>

namespace Physics {

struct BoxCollider {
    Renderer::Vec3 center{};
    Renderer::Vec3 half_extents {0.5f, 0.5f, 0.5f};
};

struct SphereCollider {
    Renderer::Vec3 center{};
    float radius = 0.5f;
};

struct CapsuleCollider {
    Renderer::Vec3 center{};
    float radius = 0.5f;
    float half_height = 0.5f;
};

struct RaycastHit {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Renderer::Vec3 position{};
    Renderer::Vec3 normal{};
    float distance = 0.0f;
};

bool raycast(
    const Ecs::World& world,
    Renderer::Vec3 origin,
    Renderer::Vec3 direction,
    float maximum_distance,
    RaycastHit *hit = nullptr
);

void overlapSphere(
    const Ecs::World& world,
    Renderer::Vec3 center,
    float radius,
    std::vector<Ecs::Entity>& out
);

void overlapAabb(
    const Ecs::World& world,
    Renderer::Vec3 minimum,
    Renderer::Vec3 maximum,
    std::vector<Ecs::Entity>& out
);

} // namespace Physics

#endif
