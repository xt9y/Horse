#ifndef HORSE_UI_ENGINE_PANELS_HPP
#define HORSE_UI_ENGINE_PANELS_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/PathTracer/PathTracer.hpp"
#include "Renderer/RayTracer/RayTracer.hpp"
#include "UI/RendererSelector.hpp"

namespace UI {

void enginePanels(
    Ecs::World& world,
    Renderer::RayTracer& ray_tracer,
    Renderer::PathTracer& path_tracer,
    RendererChoice& renderer,
    const RendererAvailability& available,
    const char *scene_name
);

} // namespace UI

#endif
