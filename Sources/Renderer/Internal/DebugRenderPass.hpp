#ifndef HORSE_RENDERER_INTERNAL_DEBUG_RENDER_PASS_HPP
#define HORSE_RENDERER_INTERNAL_DEBUG_RENDER_PASS_HPP

#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Renderer.hpp"

#include <array>
#include <vector>

namespace Renderer::Debug::RenderPass {

struct Vertex {
    std::array<float, 4> position{};
    std::array<float, 4> color{};
};

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output);

void renderSDLGPU(
    const std::vector<Vertex>& lines,
    const Scenes::Scene::CameraState& camera,
    Renderer::Internal::FrameOutput& output
);
void shutdownSDLGPU();

} // namespace Renderer::Debug::RenderPass

#endif
