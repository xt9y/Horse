#ifndef RW_ENGINE_RENDERER_SYSTEMS_OPENGL_SCENE_RESOURCES_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_OPENGL_SCENE_RESOURCES_HPP

#include "Renderer/Systems/SceneCache.hpp"

#include <cstddef>
#include <string>

namespace Renderer::Systems {

class OpenGLSceneResources {
public:
    static constexpr std::size_t MaximumTextureSlots = 16u;

    OpenGLSceneResources();
    ~OpenGLSceneResources();

    OpenGLSceneResources(const OpenGLSceneResources&) = delete;
    OpenGLSceneResources& operator=(const OpenGLSceneResources&) = delete;

    bool sync(const SceneCache& scene, std::string *error = nullptr);
    void bind();
    void clear();

    bool ready() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Systems

#endif
