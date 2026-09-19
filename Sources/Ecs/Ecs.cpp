#include "Ecs/Ecs.hpp"

#include <algorithm>
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
{
    const std::uint64_t revision = nextRevision();
    change_revision_ = revision;
    change_revisions_.fill(revision);
}

bool World::changedEntities(
    ChangeKind kind,
    std::uint64_t since,
    std::vector<Entity>& out) const
{
    out.clear();
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= entity_change_history_.size()) return false;
    if (coarse_change_revisions_[index] > since || entity_change_floors_[index] > since)
        return false;

    const auto& history = entity_change_history_[index];
    for (const ChangeEvent& event : history) {
        if (event.revision > since) out.push_back(event.entity);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

void World::touch(ChangeKind kind)
{
    const std::uint64_t revision = nextRevision();
    change_revision_ = revision;
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= change_revisions_.size()) return;

    change_revisions_[index] = revision;
    coarse_change_revisions_[index] = revision;
    entity_change_history_[index].clear();
    entity_change_floors_[index] = revision;
}

void World::touch(ChangeKind kind, Entity entity)
{
    if (!alive(entity)) {
        touch(kind);
        return;
    }

    const std::uint64_t revision = nextRevision();
    change_revision_ = revision;
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= change_revisions_.size()) return;

    change_revisions_[index] = revision;
    auto& history = entity_change_history_[index];
    history.push_back(ChangeEvent{revision, entity});

    constexpr std::size_t MinimumHistory = 256u;
    const std::size_t scaled = entities_.size() * 8u;
    const std::size_t limit = scaled > MinimumHistory ? scaled : MinimumHistory;
    while (history.size() > limit) {
        entity_change_floors_[index] = std::max(
            entity_change_floors_[index],
            history.front().revision
        );
        history.pop_front();
    }
}

void World::touchAll()
{
    const std::uint64_t revision = nextRevision();
    change_revision_ = revision;
    change_revisions_.fill(revision);
    coarse_change_revisions_.fill(revision);
    entity_change_floors_.fill(revision);
    for (auto& history : entity_change_history_) history.clear();
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

    touch(ChangeKind::Structure);
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
    touch(ChangeKind::Structure);
    return true;
}

} // namespace Ecs
