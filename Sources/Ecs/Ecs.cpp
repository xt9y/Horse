#include "Ecs/Ecs.hpp"

#include <atomic>
#include <limits>

namespace Ecs {
namespace {

std::atomic<std::uint64_t> next_revision {1u};

std::uint64_t nextRevision()
{
    return next_revision.fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

World::World()
    : change_revision_(nextRevision())
{
}

void World::touch()
{
    change_revision_ = nextRevision();
}

Entity World::createEntity()
{
    if (next_entity_ == INVALID_ENTITY) {
        throw std::overflow_error("ECS entity ID space exhausted");
    }

    const Entity entity = next_entity_++;
    const std::size_t position = entities_.size();
    entities_.push_back(entity);

    const std::size_t required = static_cast<std::size_t>(entity) + 1u;
    if (entity_sparse_.size() < required) {
        entity_sparse_.resize(required, missing_entity);
    }
    entity_sparse_[entity] = position;

    touch();
    return entity;
}

bool World::destroyEntity(Entity entity)
{
    if (!alive(entity)) return false;

    for (auto& [type, storage] : storages_) {
        (void)type;
        storage->remove(entity);
    }

    const std::size_t position = entity_sparse_[entity];
    const std::size_t last = entities_.size() - 1u;
    if (position != last) {
        const Entity moved = entities_[last];
        entities_[position] = moved;
        entity_sparse_[moved] = position;
    }

    entities_.pop_back();
    entity_sparse_[entity] = missing_entity;
    touch();
    return true;
}

} // namespace Ecs
