#ifndef HORSE_INTERACTIVITY_INTERACTIVITY_HPP
#define HORSE_INTERACTIVITY_INTERACTIVITY_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/ModelScene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Interactivity {

struct EventValue {
    std::string socket;
    std::vector<double> value;
};

struct Event {
    std::string id;
    std::vector<EventValue> values;
};

struct Limits {
    std::size_t max_nodes = 65536u;
    std::size_t max_activations_per_update = 65536u;
    std::size_t max_active_delays = 4096u;
    std::size_t max_active_interpolations = 4096u;
    std::size_t max_active_animations = 256u;
};

struct Statistics {
    std::uint64_t updates = 0u;
    std::uint64_t activations = 0u;
    std::uint64_t events = 0u;
    std::uint64_t pointer_writes = 0u;
    std::uint64_t animation_updates = 0u;
};

class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool load(
        Ecs::World& world,
        Renderer::ModelScene::Instance& instance,
        std::string *error = nullptr
    );

    bool update(Ecs::World& world, double delta_seconds, std::string *error = nullptr);

    bool send(
        Ecs::World& world,
        std::string_view event_id,
        std::span<const EventValue> values = {},
        std::string *error = nullptr
    );

    bool variable(std::size_t index, std::vector<double> *value) const;
    bool variable(std::string_view name, std::vector<double> *value) const;
    bool pollEvent(Event *event);

    void reset();

    bool active() const;
    std::string_view graphName() const;

    void setLimits(const Limits& limits);
    const Limits& limits() const;
    const Statistics& statistics() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Interactivity

#endif
