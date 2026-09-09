#include "Renderer/Renderer.hpp"

#include <cassert>

class FakeRenderer final : public Renderer::IRenderer
{
public:
    bool init() override { initialized_ = true; return true; }
    void resize(int, int) override {}
    void shutdown() override { initialized_ = false; }
    bool initialized() const override { return initialized_; }
    bool enabled() const override { return true; }
    void setEnabled(bool) override {}

    bool scene_called = false;
    bool present_called = false;

protected:
    bool renderScene(const Ecs::World&, Renderer::Internal::FrameOutput& output) override
    {
        scene_called = true;
        output.width = 1;
        output.height = 1;
        return true;
    }

    void present(Renderer::Internal::FrameOutput&) override
    {
        present_called = true;
    }

private:
    bool initialized_ = false;
};

int main()
{
    Ecs::World world;
    FakeRenderer renderer;
    renderer.render(world);
    assert(renderer.scene_called);
    assert(renderer.present_called);
    return 0;
}
