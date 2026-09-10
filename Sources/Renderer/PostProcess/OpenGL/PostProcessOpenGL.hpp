#ifndef HORSE_RENDERER_POST_PROCESS_OPENGL_HPP
#define HORSE_RENDERER_POST_PROCESS_OPENGL_HPP

#include "Renderer/PostProcess.hpp"

#include <cstdint>

namespace Renderer::PostProcess::OpenGL {

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    bool init();
    bool resize(int width, int height);
    bool begin();
    bool present(const PostProcessState& state);
    void shutdown();

    bool ready() const;
    bool depthCopiedToDefault() const;
    unsigned int depthTexture() const;
    unsigned int velocityTexture() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::PostProcess::OpenGL

#endif
