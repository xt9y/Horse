#ifndef RW_ENGINE_RENDERER_SYSTEMS_METAL_SCENE_RESOURCES_COMPAT_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_METAL_SCENE_RESOURCES_COMPAT_HPP
#ifdef __APPLE__
#include "Renderer/Scenes/Metal/SceneResources.hpp"
namespace Renderer::Systems {
using MetalSceneResources = Renderer::Scenes::Metal::SceneResources;
}
#endif
#endif
