#ifndef HORSE_RENDERER_DEBUG_DEBUG_HPP
#define HORSE_RENDERER_DEBUG_DEBUG_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"

#include <cstddef>

namespace Renderer::Debug {

struct SnapshotInfo {
    bool frozen = false;
    std::size_t visible_entities = 0u;
    std::size_t culled_entities = 0u;
    std::size_t visible_triangles = 0u;
    std::size_t culled_triangles = 0u;
    Ecs::Entity player_camera = Ecs::INVALID_ENTITY;
    Ecs::Entity debug_camera = Ecs::INVALID_ENTITY;
};

class Inspector {
public:
    Inspector();
    ~Inspector();

    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    void setShowBvh(bool value);
    void setShowViewport(bool value);
    void setBvhLevel(int value);
    void setOverlayOpacity(float value);
    void setBvhColor(Vec4 value);
    void setHighlightColor(Vec4 value);
    void setPlayerColor(Vec4 value);
    void setPlayerHeight(float value);
    void setCameraMarkerSize(float value);

    bool showBvh() const;
    bool showViewport() const;
    int bvhLevel() const;
    float overlayOpacity() const;

    bool freeze(Ecs::World& world, int width, int height);
    void unfreeze(Ecs::World& world);
    bool frozen() const;
    Ecs::Entity debugCamera() const;
    SnapshotInfo snapshotInfo() const;

    void clear(Ecs::World *world = nullptr);

private:
    struct Impl;
    Impl *impl_ = nullptr;

    friend void renderInspector(
        Inspector& inspector,
        const Ecs::World& world,
        void *frame_output
    );
};

Inspector& inspector();
void clear();
void shutdown();

} // namespace Renderer::Debug

#endif
