#include "UI/Internal.hpp"

#include <imgui.h>

#ifdef __APPLE__
#include <backends/imgui_impl_opengl2.h>
#else
#include <backends/imgui_impl_opengl3.h>
#endif

namespace UI::Internal {

bool initOpenGL()
{
#ifdef __APPLE__
    return ImGui_ImplOpenGL2_Init();
#else
    return ImGui_ImplOpenGL3_Init("#version 430");
#endif
}

void shutdownOpenGL()
{
#ifdef __APPLE__
    ImGui_ImplOpenGL2_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif
}

void newFrameOpenGL()
{
#ifdef __APPLE__
    ImGui_ImplOpenGL2_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif
}

void renderOpenGL(ImDrawData *draw_data)
{
    if (!draw_data) return;
#ifdef __APPLE__
    ImGui_ImplOpenGL2_RenderDrawData(draw_data);
#else
    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
#endif
}

} // namespace UI::Internal
