#include "Renderer/Hierarchy.hpp"

#include <algorithm>
#include <cmath>

namespace Renderer::Hierarchy {
namespace {

constexpr float pi = 3.14159265358979323846f;

bool resolveMatrix(
    const Ecs::World& world,
    Ecs::Entity entity,
    std::size_t remaining,
    Math::Mat4 *out)
{
    if (!out || !world.alive(entity)) return false;
    const Transform *local = world.get<Transform>(entity);
    if (!local) return false;

    const Math::Mat4 local_matrix = Math::modelMatrix(*local);
    const Parent *relationship = world.get<Parent>(entity);
    if (!relationship || relationship->entity == Ecs::INVALID_ENTITY ||
        !world.alive(relationship->entity) || !world.has<Transform>(relationship->entity))
    {
        *out = local_matrix;
        return true;
    }

    if (remaining == 0u || relationship->entity == entity) return false;

    Math::Mat4 parent_matrix{};
    if (!resolveMatrix(world, relationship->entity, remaining - 1u, &parent_matrix)) return false;
    *out = Math::multiply(parent_matrix, local_matrix);
    return true;
}

float length(float x, float y, float z)
{
    return std::sqrt(x * x + y * y + z * z);
}

Transform decompose(const Math::Mat4& matrix)
{
    Transform result;
    result.position = {matrix[12], matrix[13], matrix[14]};

    result.scale.x = length(matrix[0], matrix[1], matrix[2]);
    result.scale.y = length(matrix[4], matrix[5], matrix[6]);
    result.scale.z = length(matrix[8], matrix[9], matrix[10]);

    const float sx = result.scale.x > 1.0e-8f ? result.scale.x : 1.0f;
    const float sy = result.scale.y > 1.0e-8f ? result.scale.y : 1.0f;
    const float sz = result.scale.z > 1.0e-8f ? result.scale.z : 1.0f;

    const float r00 = matrix[0] / sx;
    const float r01 = matrix[4] / sy;
    const float r02 = std::clamp(matrix[8] / sz, -1.0f, 1.0f);
    const float r10 = matrix[1] / sx;
    const float r11 = matrix[5] / sy;
    const float r12 = matrix[9] / sz;
    const float r22 = matrix[10] / sz;

    const float y = std::asin(r02);
    const float cosine_y = std::cos(y);
    float x = 0.0f;
    float z = 0.0f;
    if (std::abs(cosine_y) > 1.0e-5f) {
        x = std::atan2(-r12, r22);
        z = std::atan2(-r01, r00);
    } else if (r02 >= 0.0f) {
        x = std::atan2(r10, r11);
    } else {
        x = std::atan2(-r10, r11);
    }

    constexpr float radians_to_degrees = 180.0f / pi;
    result.rotation = {
        x * radians_to_degrees,
        y * radians_to_degrees,
        z * radians_to_degrees,
    };
    result.matrix_override = matrix;
    result.matrix_override_enabled = true;
    return result;
}

bool wouldCycle(const Ecs::World& world, Ecs::Entity child, Ecs::Entity parent)
{
    Ecs::Entity current = parent;
    const std::size_t maximum = world.size() + 1u;
    for (std::size_t depth = 0u; depth < maximum; ++depth) {
        if (current == child) return true;
        const Parent *relationship = world.get<Parent>(current);
        if (!relationship || relationship->entity == Ecs::INVALID_ENTITY ||
            !world.alive(relationship->entity))
        {
            return false;
        }
        current = relationship->entity;
    }
    return true;
}

} // namespace

bool setParent(Ecs::World& world, Ecs::Entity child, Ecs::Entity parent)
{
    if (!world.alive(child)) return false;
    if (parent == Ecs::INVALID_ENTITY) return clearParent(world, child);
    if (!world.alive(parent) || child == parent || wouldCycle(world, child, parent)) return false;

    const Parent *current = world.get<Parent>(child);
    if (current && current->entity == parent) return true;

    world.add<Parent>(child, Parent{parent});
    world.markChanged(Ecs::ChangeKind::Transform);
    return true;
}

bool clearParent(Ecs::World& world, Ecs::Entity child)
{
    if (!world.alive(child)) return false;
    if (!world.remove<Parent>(child)) return true;
    world.markChanged(Ecs::ChangeKind::Transform);
    return true;
}

bool worldMatrix(const Ecs::World& world, Ecs::Entity entity, Math::Mat4 *out)
{
    return resolveMatrix(world, entity, world.size() + 1u, out);
}

bool worldTransform(const Ecs::World& world, Ecs::Entity entity, Transform *out)
{
    if (!out) return false;
    Math::Mat4 matrix{};
    if (!worldMatrix(world, entity, &matrix)) return false;
    *out = decompose(matrix);
    return true;
}

} // namespace Renderer::Hierarchy
