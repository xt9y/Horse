#include <Renderer/Components.hpp>
#include <Renderer/Reflections/Reflections.hpp>

#include <cassert>
#include <cmath>

int main()
{
    using namespace Renderer;

    Ecs::World world;

    const Ecs::Entity low = world.createEntity();
    world.add<Transform>(low, Transform{.position = {0.0f, 0.0f, 0.0f}});
    world.add<Reflections::ProbeComponent>(low, Reflections::ProbeComponent{
        .texture = static_cast<Models::TextureHandle>(7u),
        .half_extents = {10.0f, 10.0f, 10.0f},
        .blend_distance = 2.0f,
        .intensity = 0.75f,
        .priority = 0,
    });

    const Ecs::Entity high_a = world.createEntity();
    world.add<Transform>(high_a, Transform{.position = {2.0f, 0.0f, 0.0f}});
    world.add<Reflections::ProbeComponent>(high_a, Reflections::ProbeComponent{
        .texture = static_cast<Models::TextureHandle>(8u),
        .half_extents = {10.0f, 10.0f, 10.0f},
        .blend_distance = 2.0f,
        .intensity = 1.0f,
        .priority = 2,
    });

    const Ecs::Entity high_b = world.createEntity();
    world.add<Transform>(high_b, Transform{.position = {-2.0f, 0.0f, 0.0f}});
    world.add<Reflections::ProbeComponent>(high_b, Reflections::ProbeComponent{
        .texture = static_cast<Models::TextureHandle>(9u),
        .half_extents = {10.0f, 10.0f, 10.0f},
        .blend_distance = 2.0f,
        .intensity = 1.0f,
        .priority = 2,
    });

    const Ecs::Entity disabled = world.createEntity();
    world.add<Transform>(disabled, Transform{});
    world.add<Reflections::ProbeComponent>(disabled, Reflections::ProbeComponent{
        .enabled = false,
        .texture = static_cast<Models::TextureHandle>(10u),
    });

    const Ecs::Entity invalid = world.createEntity();
    world.add<Transform>(invalid, Transform{});
    world.add<Reflections::ProbeComponent>(invalid, Reflections::ProbeComponent{});

    const Reflections::State state = Reflections::state(world);
    assert(state.probes.size() == 3u);
    assert(state.probes[0].entity == high_a);
    assert(state.probes[1].entity == high_b);
    assert(state.probes[2].entity == low);

    assert(Reflections::probeInfluence(state.probes[0], {2.0f, 0.0f, 0.0f}) == 1.0f);
    assert(Reflections::probeInfluence(state.probes[0], {12.0f, 0.0f, 0.0f}) == 0.0f);
    assert(std::abs(
        Reflections::probeInfluence(state.probes[0], {11.0f, 0.0f, 0.0f}) - 0.5f
    ) < 1.0e-6f);

    const Reflections::Blend center = Reflections::blend(state, {0.0f, 0.0f, 0.0f});
    assert(center.count == 2u);
    assert(center.indices[0] == 0u);
    assert(center.indices[1] == 1u);
    assert(std::abs(center.weights[0] - 0.5f) < 1.0e-6f);
    assert(std::abs(center.weights[1] - 0.5f) < 1.0e-6f);

    const Reflections::Blend high_only = Reflections::blend(state, {8.5f, 0.0f, 0.0f});
    assert(high_only.count == 1u);
    assert(high_only.indices[0] == 0u);
    assert(high_only.weights[0] == 1.0f);

    const Reflections::Blend none = Reflections::blend(state, {100.0f, 0.0f, 0.0f});
    assert(none.count == 0u);

    return 0;
}
