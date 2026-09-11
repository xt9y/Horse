#ifndef HORSE_RENDERER_FRAME_METAL_HPP
#define HORSE_RENDERER_FRAME_METAL_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer::Frame::Metal {

class Presenter {
public:
    Presenter();
    ~Presenter();

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    bool init();
    bool compose(Internal::FrameOutput& output);
    void shutdown();

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

bool beginClear(Internal::FrameOutput& output);
void present(Internal::FrameOutput& output);

} // namespace Renderer::Frame::Metal

#endif
