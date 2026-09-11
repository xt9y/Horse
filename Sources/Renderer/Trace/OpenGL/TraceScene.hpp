#ifndef RW_ENGINE_RENDERER_TRACE_OPENGL_TRACE_SCENE_HPP
#define RW_ENGINE_RENDERER_TRACE_OPENGL_TRACE_SCENE_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Scenes/OpenGL/SceneResources.hpp"

#include <cstddef>

namespace Renderer::Trace::OpenGL {

class TraceScene {
public:
    static constexpr std::size_t MaximumTextureSlots =
        Scenes::OpenGL::SceneResources::MaximumTextureSlots;

    struct SyncResult {
        bool ok = false;
        bool scene_changed = false;
    };

    TraceScene();
    ~TraceScene();

    TraceScene(const TraceScene&) = delete;
    TraceScene& operator=(const TraceScene&) = delete;

    SyncResult sync(const Ecs::World& world, int width, int height, const char *owner);
    void bind() const;
    void clear();

    std::size_t nodeCount() const;
    std::size_t triangleCount() const;
    std::size_t materialCount() const;
    bool visibilityAll() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Trace::OpenGL

#endif
