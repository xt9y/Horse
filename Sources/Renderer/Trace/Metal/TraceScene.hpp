#ifndef RW_ENGINE_RENDERER_TRACE_METAL_TRACE_SCENE_HPP
#define RW_ENGINE_RENDERER_TRACE_METAL_TRACE_SCENE_HPP

#ifdef __APPLE__

#include "Ecs/Ecs.hpp"

#include <lwmgl/lwmgl.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Renderer::Trace::Metal {

class TraceScene {
public:
    struct SyncResult {
        bool ok = false;
        bool scene_changed = false;
    };

    TraceScene();
    ~TraceScene();

    TraceScene(const TraceScene&) = delete;
    TraceScene& operator=(const TraceScene&) = delete;

    bool init(std::string *error = nullptr);
    SyncResult sync(const Ecs::World& world, int width, int height, const char *owner);
    bool bind(LWMGLCommand command, std::uint32_t first_texture_binding = 1u) const;
    void clear();

    std::size_t triangleCount() const;
    std::size_t materialCount() const;
    bool hasAlphaCutouts() const;
    bool visibilityAll() const;
    LWMGLAccelerationStructure accelerationStructure() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Trace::Metal

#endif
#endif
