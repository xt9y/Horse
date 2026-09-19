#include <Ecs/Ecs.hpp>

#include <algorithm>
#include <cassert>
#include <vector>

int main()
{
    Ecs::World world;
    const Ecs::Entity first = world.createEntity();
    const Ecs::Entity second = world.createEntity();

    const std::uint64_t initial = world.changeRevision(Ecs::ChangeKind::Transform);
    std::vector<Ecs::Entity> changed;
    assert(world.changedEntities(Ecs::ChangeKind::Transform, initial, changed));
    assert(changed.empty());

    world.markChanged(Ecs::ChangeKind::Transform, first);
    world.markChanged(Ecs::ChangeKind::Transform, second);
    assert(world.changedEntities(Ecs::ChangeKind::Transform, initial, changed));
    std::sort(changed.begin(), changed.end());
    assert(changed.size() == 2u);
    assert(changed[0] == first);
    assert(changed[1] == second);

    const std::uint64_t exact = world.changeRevision(Ecs::ChangeKind::Transform);
    world.markChanged(Ecs::ChangeKind::Transform, first);
    assert(world.changedEntities(Ecs::ChangeKind::Transform, exact, changed));
    assert(changed.size() == 1u);
    assert(changed.front() == first);

    const std::uint64_t coarse = world.changeRevision(Ecs::ChangeKind::Transform);
    world.markChanged(Ecs::ChangeKind::Transform);
    assert(!world.changedEntities(Ecs::ChangeKind::Transform, coarse, changed));

    return 0;
}
