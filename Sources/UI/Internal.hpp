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
void newFrameOpenGL();
void renderOpenGL(ImDrawData *draw_data);

#ifdef __APPLE__
bool initMetal();
void shutdownMetal();
bool prepareMetal(Renderer::Internal::FrameOutput& output);
bool renderMetal(ImDrawData *draw_data, Renderer::Internal::FrameOutput& output);
#endif

bool renderPersistentOverlays(const Ecs::World& world);

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output);
void shutdownRendererBackend();

} // namespace UI::Internal

#endif
