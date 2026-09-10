#ifndef HORSE_RENDERER_HIERARCHY_HPP
#define HORSE_RENDERER_HIERARCHY_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Math.hpp"

namespace Renderer::Hierarchy {

bool setParent(Ecs::World& world, Ecs::Entity child, Ecs::Entity parent);
bool clearParent(Ecs::World& world, Ecs::Entity child);
bool worldMatrix(const Ecs::World& world, Ecs::Entity entity, Math::Mat4 *out);
bool worldTransform(const Ecs::World& world, Ecs::Entity entity, Transform *out);

} // namespace Renderer::Hierarchy

#endif
