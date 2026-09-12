#ifndef HORSE_RENDERER_MODEL_SCENE_HPP
#define HORSE_RENDERER_MODEL_SCENE_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Models/Runtime.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::ModelScene {

struct PartBinding {
    std::uint32_t part = Models::INVALID_INDEX;
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Models::MeshHandle mesh = Models::INVALID_MESH;
    bool dynamic_mesh = false;
};

struct NodeBinding {
    std::uint32_t node = Models::INVALID_INDEX;
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    std::vector<PartBinding> parts;
};

struct Instance {
    Models::ModelHandle model = Models::INVALID_MODEL;
    std::uint32_t scene = Models::INVALID_INDEX;
    std::uint32_t variant = Models::INVALID_INDEX;
    std::vector<NodeBinding> nodes;
    std::vector<PartBinding> loose_parts;
};

struct Options {
    std::uint32_t scene = Models::INVALID_INDEX;
    std::uint32_t variant = Models::INVALID_INDEX;
    bool instantiate_cameras = true;
    bool instantiate_lights = true;
    bool activate_first_camera = false;
};

bool instantiate(
    Ecs::World& world,
    Models::ModelHandle model,
    Instance *output,
    const Options& options = {},
    std::string *error = nullptr
);

bool applyPose(
    Ecs::World& world,
    Instance& instance,
    const Models::Runtime::Pose& pose,
    std::string *error = nullptr
);

bool setVariant(
    Ecs::World& world,
    Instance& instance,
    std::uint32_t variant,
    std::string *error = nullptr
);

void destroy(Ecs::World& world, Instance& instance);

} // namespace Renderer::ModelScene

#endif
