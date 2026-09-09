#ifndef RW_ENGINE_RENDERER_SYSTEMS_METAL_SCENE_RESOURCES_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_METAL_SCENE_RESOURCES_HPP

#ifdef __APPLE__

#include "Renderer/Systems/SceneCache.hpp"

#include <lwmgl/lwmgl.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Renderer::Systems {

class MetalSceneResources {
public:
    static constexpr std::size_t MaximumTextureSlots = 32u;

    MetalSceneResources();
    ~MetalSceneResources();

    MetalSceneResources(const MetalSceneResources&) = delete;
    MetalSceneResources& operator=(const MetalSceneResources&) = delete;

    bool init(std::string *error = nullptr);
    bool sync(const SceneCache& scene, std::string *error = nullptr);
    bool bind(LWMGLCommand command, std::uint32_t first_texture_binding = 1u) const;
    void clear();

    bool ready() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Systems

#endif
#endif
