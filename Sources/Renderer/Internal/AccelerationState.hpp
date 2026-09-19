#ifndef HORSE_RENDERER_INTERNAL_ACCELERATION_STATE_HPP
#define HORSE_RENDERER_INTERNAL_ACCELERATION_STATE_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Scenes/Acceleration.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::Internal {

class AccelerationState {
public:
    bool sync(const Ecs::World& world, std::string *error = nullptr);
    void clear();

    const Scenes::AccelerationScene& acceleration() const { return acceleration_; }
    const Scenes::SceneCache& scene() const { return scene_; }
    const std::vector<Scenes::Scene::RenderItem>& renderItems() const { return render_items_; }
    const std::vector<Scenes::Scene::RenderItem>& dynamicItems() const { return dynamic_items_; }

    std::uint64_t synchronizations() const { return synchronizations_; }

private:
    const Ecs::World *world_ = nullptr;
    Scenes::Scene::RenderRevision revision_{};
    bool initialized_ = false;
    std::uint64_t synchronizations_ = 0u;

    Scenes::AccelerationScene acceleration_;
    Scenes::SceneCache scene_;
    std::vector<Scenes::Scene::RenderItem> render_items_;
    std::vector<Scenes::Scene::RenderItem> dynamic_items_;
};

AccelerationState& accelerationState();
bool syncAccelerationState(const Ecs::World& world, std::string *error = nullptr);
void clearAccelerationState();

class SharedSceneCacheView {
public:
    bool syncResources(
        const Ecs::World& world,
        const std::vector<Scenes::Scene::RenderItem>&,
        std::size_t,
        std::string *error = nullptr)
    {
        return syncAccelerationState(world, error);
    }

    bool syncGeometry(
        const Ecs::World& world,
        const std::vector<Scenes::Scene::RenderItem>&,
        std::string *error = nullptr)
    {
        return syncAccelerationState(world, error);
    }

    void clear() {}

    const auto& nodes() const { return accelerationState().scene().nodes(); }
    const auto& triangles() const { return accelerationState().scene().triangles(); }
    const auto& materials() const { return accelerationState().scene().materials(); }
    const auto& textureHandles() const { return accelerationState().scene().textureHandles(); }
    std::uint32_t materialIndex(Models::MaterialHandle handle) const
    {
        return accelerationState().scene().materialIndex(handle);
    }

    operator const Scenes::SceneCache&() const { return accelerationState().scene(); }
};

class SharedAccelerationView {
public:
    bool sync(
        const Ecs::World& world,
        const std::vector<Scenes::Scene::RenderItem>&,
        std::string *error = nullptr)
    {
        return syncAccelerationState(world, error);
    }

    void clear() {}

    const auto& tlasNodes() const { return accelerationState().acceleration().tlasNodes(); }
    const auto& blasNodes() const { return accelerationState().acceleration().blasNodes(); }
    const auto& localTriangles() const { return accelerationState().acceleration().localTriangles(); }
    const auto& blases() const { return accelerationState().acceleration().blases(); }
    const auto& instances() const { return accelerationState().acceleration().instances(); }

    operator const Scenes::AccelerationScene&() const
    {
        return accelerationState().acceleration();
    }
};

} // namespace Renderer::Internal

#endif
