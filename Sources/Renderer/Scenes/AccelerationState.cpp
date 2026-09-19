#include "Renderer/Internal/AccelerationState.hpp"

#include <algorithm>
#include <limits>

namespace Renderer::Internal {
namespace {

bool sameAccelerationRevision(
    const Scenes::Scene::RenderRevision& a,
    const Scenes::Scene::RenderRevision& b)
{
    return a.structure == b.structure &&
        a.transform == b.transform &&
        a.resource == b.resource &&
        a.animation == b.animation &&
        a.camera == b.camera &&
        a.model_resource == b.model_resource;
}

bool transformOnlyRevision(
    const Scenes::Scene::RenderRevision& current,
    const Scenes::Scene::RenderRevision& previous)
{
    return current.transform != previous.transform &&
        current.structure == previous.structure &&
        current.resource == previous.resource &&
        current.animation == previous.animation &&
        current.camera == previous.camera &&
        current.model_resource == previous.model_resource;
}

void collectDynamicItems(
    const Ecs::World& world,
    const std::vector<Scenes::Scene::RenderItem>& items,
    std::vector<Scenes::Scene::RenderItem>& dynamic_items)
{
    dynamic_items.clear();
    dynamic_items.reserve(items.size());
    for (const Scenes::Scene::RenderItem& item : items) {
        if (item.layer != RenderLayer::World) continue;
        if (!Scenes::AccelerationScene::eligible(world, item))
            dynamic_items.push_back(item);
    }
}

} // namespace

void AccelerationState::rebuildTransformDependencies(const Ecs::World& world)
{
    transform_dependents_.clear();
    render_item_marks_.assign(render_items_.size(), 0u);
    render_item_generation_ = 0u;

    const std::size_t maximum = world.size() + 1u;
    for (std::size_t item_index = 0u; item_index < render_items_.size(); ++item_index) {
        Ecs::Entity current = render_items_[item_index].entity;
        for (std::size_t depth = 0u; depth < maximum; ++depth) {
            if (current == Ecs::INVALID_ENTITY || !world.alive(current)) break;
            transform_dependents_[current].push_back(item_index);

            const Parent *parent = world.get<Parent>(current);
            if (!parent || parent->entity == Ecs::INVALID_ENTITY ||
                !world.alive(parent->entity) || parent->entity == current)
                break;
            current = parent->entity;
        }
    }
}

bool AccelerationState::collectDirtyRenderItems(const std::vector<Ecs::Entity>& entities)
{
    dirty_render_items_.clear();
    if (render_item_marks_.size() != render_items_.size()) return false;

    ++render_item_generation_;
    if (render_item_generation_ == 0u) {
        std::fill(render_item_marks_.begin(), render_item_marks_.end(), 0u);
        render_item_generation_ = 1u;
    }

    for (const Ecs::Entity entity : entities) {
        const auto found = transform_dependents_.find(entity);
        if (found == transform_dependents_.end()) continue;
        for (const std::size_t item_index : found->second) {
            if (item_index >= render_items_.size()) return false;
            if (render_item_marks_[item_index] == render_item_generation_) continue;
            render_item_marks_[item_index] = render_item_generation_;
            dirty_render_items_.push_back(item_index);
        }
    }
    return true;
}

bool AccelerationState::sync(const Ecs::World& world, std::string *error)
{
    if (error) error->clear();

    const Scenes::Scene::RenderRevision current = Scenes::Scene::renderRevision(world);
    const std::uint64_t current_config_revision = Scenes::SceneCache::configRevision();
    if (initialized_ &&
        world_ == &world &&
        config_revision_ == current_config_revision &&
        sameAccelerationRevision(current, revision_))
        return true;

    const bool transform_only = initialized_ &&
        world_ == &world &&
        config_revision_ == current_config_revision &&
        transformOnlyRevision(current, revision_);

    if (transform_only) {
        const bool exact_changes = world.changedEntities(
            Ecs::ChangeKind::Transform,
            revision_.transform,
            dirty_entities_
        ) && collectDirtyRenderItems(dirty_entities_);

        bool partial = exact_changes;
        if (partial && !Scenes::Scene::refreshRenderItemTransforms(
                world,
                render_items_,
                dirty_render_items_))
            partial = false;

        if (!partial) {
            if (!Scenes::Scene::refreshRenderItemTransforms(world, render_items_)) {
                Scenes::Scene::collectRenderItems(world, render_items_);
                rebuildTransformDependencies(world);
            }
        }

        if (!dynamic_items_.empty()) {
            collectDynamicItems(world, render_items_, dynamic_items_);
            if (!scene_.syncGeometry(world, dynamic_items_, error)) {
                clear();
                return false;
            }
        }

        const bool acceleration_ok = partial
            ? acceleration_.syncTransforms(world, render_items_, dirty_render_items_, error)
            : acceleration_.syncTransforms(world, render_items_, error);
        if (!acceleration_ok) {
            clear();
            return false;
        }
    } else {
        Scenes::Scene::collectRenderItems(world, render_items_);
        rebuildTransformDependencies(world);
        if (!scene_.syncResources(
                world,
                render_items_,
                std::numeric_limits<std::size_t>::max(),
                error))
        {
            clear();
            return false;
        }

        collectDynamicItems(world, render_items_, dynamic_items_);
        if (!scene_.syncGeometry(world, dynamic_items_, error) ||
            !acceleration_.sync(world, render_items_, error))
        {
            clear();
            return false;
        }
    }

    world_ = &world;
    revision_ = current;
    config_revision_ = current_config_revision;
    initialized_ = true;
    ++synchronizations_;
    return true;
}

void AccelerationState::clear()
{
    world_ = nullptr;
    revision_ = {};
    config_revision_ = 0u;
    initialized_ = false;
    acceleration_.clear();
    scene_.clear();
    render_items_.clear();
    dynamic_items_.clear();
    transform_dependents_.clear();
    render_item_marks_.clear();
    render_item_generation_ = 0u;
    dirty_entities_.clear();
    dirty_render_items_.clear();
}

AccelerationState& accelerationState()
{
    static AccelerationState state;
    return state;
}

bool syncAccelerationState(const Ecs::World& world, std::string *error)
{
    return accelerationState().sync(world, error);
}

void clearAccelerationState()
{
    accelerationState().clear();
}

} // namespace Renderer::Internal
