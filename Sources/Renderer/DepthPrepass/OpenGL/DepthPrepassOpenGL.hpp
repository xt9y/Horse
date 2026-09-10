#ifndef HORSE_RENDERER_DEPTH_PREPASS_OPENGL_HPP
#define HORSE_RENDERER_DEPTH_PREPASS_OPENGL_HPP

#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <cstddef>
#include <vector>

namespace Renderer::Systems::OpenGL {
class TextureCache;
}

namespace Renderer::DepthPrepass {

struct OpenGLInput {
    const std::vector<Scenes::Scene::RenderItem> *items = nullptr;
    Math::Mat4 projection = Math::identityMatrix();
    Math::Mat4 view = Math::identityMatrix();
    int width = 1;
    int height = 1;
    float alpha_cutoff = 0.5f;
};

struct Statistics {
    std::size_t drawn_items = 0u;
};

class OpenGLPass {
public:
    OpenGLPass();
    ~OpenGLPass();

    OpenGLPass(const OpenGLPass&) = delete;
    OpenGLPass& operator=(const OpenGLPass&) = delete;

    bool render(const OpenGLInput& input, Systems::OpenGL::TextureCache& textures);
    void shutdown();
    Statistics statistics() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::DepthPrepass

#endif
