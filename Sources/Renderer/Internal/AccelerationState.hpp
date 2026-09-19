#ifndef HORSE_RENDERER_INTERNAL_ACCELERATION_STATE_HPP
#define HORSE_RENDERER_INTERNAL_ACCELERATION_STATE_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Scenes/Acceleration.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

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

} // namespace Renderer::Internal

#endif
