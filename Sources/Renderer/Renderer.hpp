#ifndef RW_ENGINE_RENDERER_RENDERER_HPP
#define RW_ENGINE_RENDERER_RENDERER_HPP

#include "Ecs/Ecs.hpp"

namespace Renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init() = 0;
    virtual void resize(int width, int height) = 0;
    virtual void render(const Ecs::World& world) = 0;
    virtual void shutdown() = 0;

    virtual bool initialized() const = 0;
    virtual bool enabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};

} // namespace Renderer

#endif
