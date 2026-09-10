#ifndef HORSE_UI_INTERNAL_HPP
#define HORSE_UI_INTERNAL_HPP

#include "Renderer/Renderer.hpp"

struct ImDrawData;

namespace UI::Internal {

enum class Backend {
    None,
    OpenGL,
    Metal,
};

bool initOpenGL();
void shutdownOpenGL();
void renderOpenGL(ImDrawData *draw_data);

#ifdef __APPLE__
bool initMetal();
void shutdownMetal();
bool renderMetal(ImDrawData *draw_data, Renderer::Internal::FrameOutput& output);
#endif

void render(Renderer::Internal::FrameOutput& output);
void shutdownRendererBackend();

} // namespace UI::Internal

#endif
