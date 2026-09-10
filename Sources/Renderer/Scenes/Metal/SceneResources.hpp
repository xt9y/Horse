#ifndef RW_ENGINE_RENDERER_SCENES_METAL_SCENE_RESOURCES_HPP
#define RW_ENGINE_RENDERER_SCENES_METAL_SCENE_RESOURCES_HPP

#ifdef __APPLE__

#include "Renderer/Scenes/SceneCache.hpp"

#include <lwmgl/lwmgl.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::Scenes::Metal {

class SceneResources {
public:
    static constexpr std::size_t MaximumTextureSlots = 32u;

    SceneResources();
    ~SceneResources();

    SceneResources(const SceneResources&) = delete;
    SceneResources& operator=(const SceneResources&) = delete;

    bool init(std::string *error = nullptr);
    bool sync(const Renderer::Scenes::SceneCache& scene, std::string *error = nullptr);
    bool syncVisibility(const std::vector<std::uint32_t>& visibility, std::string *error = nullptr);
    bool bind(LWMGLCommand command, std::uint32_t first_texture_binding = 1u) const;
    void clear();

    bool ready() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Scenes::Metal

#endif
#endif
