#ifndef RW_ENGINE_RENDERER_RASTERIZER_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer {

class Rasterizer final : public IRenderer {
public:
    struct Impl;

    Rasterizer();
    ~Rasterizer() override;

    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    bool init() override;
    void resize(int width, int height) override;
    void shutdown() override;

    bool initialized() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

protected:
    bool renderScene(const Ecs::World& world, Internal::FrameOutput& output) override;
    void present(Internal::FrameOutput& output) override;

private:
    Impl* impl_;
};

} // namespace Renderer

#endif
