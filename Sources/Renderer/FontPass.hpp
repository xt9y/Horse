#ifndef RW_ENGINE_RENDERER_FONT_PASS_HPP
#define RW_ENGINE_RENDERER_FONT_PASS_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Renderer.hpp"

#include <array>
#include <vector>

namespace Renderer::Internal {

struct FontVertex {
    std::array<float, 4> clip{};
    std::array<float, 4> uv_depth{};
    std::array<float, 4> color{};
};

struct FontBatches {
    std::vector<FontVertex> depth;
    std::vector<FontVertex> overlay;
};

bool fontDepthRequired(const Ecs::World& world);
void collectFontVertices(
    const Ecs::World& world,
    const FrameOutput& output,
    FontBatches& batches
);
void renderFonts(const Ecs::World& world, FrameOutput& output);
void shutdownFonts(GraphicsApi api);

void renderFontsOpenGL(const FontBatches& batches, FrameOutput& output);
void shutdownFontsOpenGL();

#ifdef __APPLE__
void renderFontsMetal(const FontBatches& batches, FrameOutput& output);
void shutdownFontsMetal();
#endif

} // namespace Renderer::Internal

#endif
