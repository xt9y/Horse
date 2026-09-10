#ifndef HORSE_RENDERER_DEBUG_INTERNAL_HPP
#define HORSE_RENDERER_DEBUG_INTERNAL_HPP

#include "Renderer/Math.hpp"
#include "Renderer/Renderer.hpp"

#include <array>
#include <vector>

namespace Renderer::Debug::Internal {

struct Vertex {
    std::array<float, 4> position{};
    std::array<float, 4> color{};
};

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output);

void renderOpenGL(
    const std::vector<Vertex>& lines,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output
);
void shutdownOpenGL();

#ifdef __APPLE__
void renderMetal(
    const std::vector<Vertex>& lines,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output
);
void shutdownMetal();
#endif

} // namespace Renderer::Debug::Internal

#endif
