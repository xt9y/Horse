#ifndef RW_ENGINE_RENDERER_RENDERER_HPP
#define RW_ENGINE_RENDERER_RENDERER_HPP

#include "Ecs/Ecs.hpp"

#include <cstdint>

namespace Renderer {

namespace GlobalIllumination {
struct Field;
}

namespace Internal {

enum class GraphicsApi : std::uint8_t {
    OpenGL,
    Metal,
};

enum class DepthSource : std::uint8_t {
    None,
    Native,
    LinearTexture,
};

struct FrameOutput {
    GraphicsApi api = GraphicsApi::OpenGL;
    DepthSource depth = DepthSource::None;
    int width = 1;
    int height = 1;
    void *command = nullptr;
    void *depth_texture = nullptr;
    const GlobalIllumination::Field *global_illumination = nullptr;
};

} // namespace Internal

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init() = 0;
    virtual void resize(int width, int height) = 0;
    void render(const Ecs::World& world);
    virtual void shutdown() = 0;

    virtual bool initialized() const = 0;
    virtual bool enabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;

protected:
    virtual bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) = 0;
    virtual void present(Internal::FrameOutput& output) = 0;
};

} // namespace Renderer

#endif
