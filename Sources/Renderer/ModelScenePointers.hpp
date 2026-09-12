#ifndef HORSE_RENDERER_MODEL_SCENE_POINTERS_HPP
#define HORSE_RENDERER_MODEL_SCENE_POINTERS_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Runtime.hpp"

#include <string>

namespace Renderer::ModelScene {

struct Instance;

namespace Pointers {

bool apply(
    Ecs::World& world,
    Instance& instance,
    const Models::Runtime::Pose& pose,
    std::string *error = nullptr
);

} // namespace Pointers
} // namespace Renderer::ModelScene

#endif
