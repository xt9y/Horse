#ifndef HORSE_RENDERER_RENDERER_HPP
#define HORSE_RENDERER_RENDERER_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/PostProcess.hpp"

#include <cstdint>

namespace Renderer {

namespace GlobalIllumination {
struct Field;
}

namespace Internal {

using GraphicsApi = PostProcess::GraphicsApi;
using DepthSource = PostProcess::DepthSource;

struct FrameOutput : PostProcess::Frame {
    const GlobalIllumination::Field *global_illumination = nullptr;
    void *scene_resources = nullptr;
    void *display_texture = nullptr;
    void *presentation_texture = nullptr;
};

} // namespace Internal

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init() = 0;
    virtual bool activate() { return initialized(); }
    virtual void deactivate() {}
    virtual void resize(int width, int height) = 0;
    void setPostProcessPipeline(PostProcess::Pipeline *pipeline) { post_process_ = pipeline; }
    void render(const Ecs::World& world);
    virtual void shutdown() = 0;

    virtual bool initialized() const = 0;
    virtual bool enabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;

protected:
    virtual bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) = 0;
    virtual bool compose(Internal::FrameOutput& output) = 0;
    virtual void present(Internal::FrameOutput& output) = 0;

private:
    PostProcess::Pipeline *post_process_ = nullptr;
};

} // namespace Renderer

#endif
