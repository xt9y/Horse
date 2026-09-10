#ifndef HORSE_UI_ENGINE_PANELS_HPP
#define HORSE_UI_ENGINE_PANELS_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/PathTracer/PathTracer.hpp"
#include "Renderer/RayTracer/RayTracer.hpp"
#include "UI/RendererSelector.hpp"

#include <cstddef>

namespace UI {

struct SceneSelection {
    const char *const *names = nullptr;
    std::size_t count = 0u;
    std::size_t selected = 0u;
};

void enginePanels(
    Ecs::World& world,
    Renderer::RayTracer& ray_tracer,
    Renderer::PathTracer& path_tracer,
    RendererChoice& renderer,
    const RendererAvailability& available,
    SceneSelection& scenes
);

} // namespace UI

#endif
