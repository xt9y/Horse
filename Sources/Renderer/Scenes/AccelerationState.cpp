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
        a.model_resource == b.model_resource;
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

    Scenes::Scene::collectRenderItems(world, render_items_);

    if (!scene_.syncResources(
            world,
            render_items_,
            std::numeric_limits<std::size_t>::max(),
            error))
    {
        clear();
        return false;
    }

    dynamic_items_.clear();
    dynamic_items_.reserve(render_items_.size());
    for (const Scenes::Scene::RenderItem& item : render_items_) {
        if (item.layer != RenderLayer::World) continue;
        if (!Scenes::AccelerationScene::eligible(world, item))
            dynamic_items_.push_back(item);
    }

    if (!scene_.syncGeometry(world, dynamic_items_, error) ||
        !acceleration_.sync(world, render_items_, error))
    {
        clear();
        return false;
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
