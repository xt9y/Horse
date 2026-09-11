#ifndef HORSE_RENDERER_FRAME_OPENGL_HPP
#define HORSE_RENDERER_FRAME_OPENGL_HPP

#include "Renderer/Renderer.hpp"

namespace Renderer::Frame::OpenGL {

class Target {
public:
    Target();
    ~Target();

    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    bool init();
    bool resize(int width, int height);
    bool begin();
    void finish(Internal::FrameOutput& output);
    bool compose(Internal::FrameOutput& output);
    void shutdown();

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

bool presentTexture(void *texture, int width, int height);

} // namespace Renderer::Frame::OpenGL

#endif
