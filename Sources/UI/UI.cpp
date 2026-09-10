#include "UI/UI.hpp"

#include "UI/Internal.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <lwcgl/lwcgl.h>

namespace UI {
namespace {

bool ready = false;
bool frame_active = false;
Internal::Backend backend = Internal::Backend::None;

Internal::Backend backendFor(Renderer::Internal::GraphicsApi api)
{
    switch (api) {
        case Renderer::Internal::GraphicsApi::OpenGL: return Internal::Backend::OpenGL;
        case Renderer::Internal::GraphicsApi::Metal: return Internal::Backend::Metal;
    }
    return Internal::Backend::None;
}

bool activateBackend(Internal::Backend requested)
{
    if (backend == requested) return true;
    Internal::shutdownRendererBackend();

    bool ok = false;
    switch (requested) {
        case Internal::Backend::OpenGL:
            ok = Internal::initOpenGL();
            break;
        case Internal::Backend::Metal:
#ifdef __APPLE__
            ok = Internal::initMetal();
#endif
            break;
        case Internal::Backend::None:
            ok = true;
            break;
    }

    backend = ok ? requested : Internal::Backend::None;
    return ok;
}

} // namespace

bool init()
{
    if (ready) return true;
    if (!Display.isCreated() || !Display.getNativeWindow()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOther(
            static_cast<GLFWwindow *>(Display.getNativeWindow()),
            true))
    {
        ImGui::DestroyContext();
        return false;
    }

    if (!activateBackend(Internal::Backend::OpenGL)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    ready = true;
    return true;
}

void shutdown()
{
    if (!ready) return;

    if (frame_active) {
        ImGui::EndFrame();
        frame_active = false;
    }

    Internal::shutdownRendererBackend();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    backend = Internal::Backend::None;
    ready = false;
}

void beginFrame()
{
    if (!ready && !init()) return;

    if (frame_active) ImGui::EndFrame();
    ImGui_ImplGlfw_NewFrame();
    if (backend == Internal::Backend::OpenGL)
        Internal::newFrameOpenGL();
    ImGui::NewFrame();
    frame_active = true;
}

bool initialized()
{
    return ready;
}

bool wantsMouse()
{
    return ready && ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboard()
{
    return ready && ImGui::GetIO().WantCaptureKeyboard;
}

namespace Internal {

void shutdownRendererBackend()
{
    switch (backend) {
        case Backend::OpenGL:
            shutdownOpenGL();
            break;
        case Backend::Metal:
#ifdef __APPLE__
            shutdownMetal();
#endif
            break;
        case Backend::None:
            break;
    }
    backend = Backend::None;
}

void render(Renderer::Internal::FrameOutput& output)
{
    if (!ready || !frame_active) return;

    ImGui::Render();
    frame_active = false;

    const Backend requested = backendFor(output.api);
    if (requested != backend) {
        if (!activateBackend(requested)) return;
#ifdef __APPLE__
        if (backend == Backend::Metal)
            (void)prepareMetal(output);
#endif
        return;
    }

    ImDrawData *draw_data = ImGui::GetDrawData();
    if (!draw_data) return;

    switch (backend) {
        case Backend::OpenGL:
            renderOpenGL(draw_data);
            break;
        case Backend::Metal:
#ifdef __APPLE__
            (void)renderMetal(draw_data, output);
#endif
            break;
        case Backend::None:
            break;
    }
}

} // namespace Internal
} // namespace UI
