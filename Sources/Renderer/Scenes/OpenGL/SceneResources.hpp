#ifndef RW_ENGINE_RENDERER_SCENES_OPENGL_SCENE_RESOURCES_HPP
#define RW_ENGINE_RENDERER_SCENES_OPENGL_SCENE_RESOURCES_HPP

#include "Renderer/Scenes/SceneCache.hpp"

#include <cstddef>
#include <string>

namespace Renderer::Scenes::OpenGL {

class SceneResources {
public:
    static constexpr std::size_t MaximumTextureSlots = 16u;

    SceneResources();
    ~SceneResources();

    SceneResources(const SceneResources&) = delete;
    SceneResources& operator=(const SceneResources&) = delete;

    bool sync(const Renderer::Scenes::SceneCache& scene, std::string *error = nullptr);
    void bind();
    void clear();

    bool ready() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Scenes::OpenGL

#endif
