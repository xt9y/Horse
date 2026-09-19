#include "Renderer/Internal/AccelerationState.hpp"

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

    Scenes::Scene::collectRenderItems(world, render_items_);

    if (transform_only) {
        if (!dynamic_items_.empty()) {
            collectDynamicItems(world, render_items_, dynamic_items_);
            if (!scene_.syncGeometry(world, dynamic_items_, error)) {
                clear();
                return false;
            }
        }

        if (!acceleration_.syncTransforms(world, render_items_, error)) {
            clear();
            return false;
        }
    } else {
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
