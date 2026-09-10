#include "UI/UI.hpp"

#include "UI/Internal.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <lwcgl/lwcgl.h>

namespace UI {
namespace {

bool ready = false;
bool frame_active = false;
bool frame_visible = false;
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

void applyStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(7.0f, 6.0f);
    style.FramePadding = ImVec2(4.0f, 2.0f);
    style.ItemSpacing = ImVec2(5.0f, 3.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
    style.WindowRounding = 4.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 8.0f;

    style.FontScaleMain = 0.82f;
    style.ScaleAllSizes(0.82f);
    style.FrameRounding = 0.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.88f, 0.89f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.42f, 0.44f, 0.49f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.027f, 0.032f, 0.97f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.025f, 0.027f, 0.032f, 0.97f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.035f, 0.039f, 0.048f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.27f, 0.90f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.055f, 0.105f, 0.175f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.085f, 0.205f, 0.350f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.115f, 0.290f, 0.505f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.025f, 0.030f, 0.040f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.060f, 0.155f, 0.285f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.025f, 0.030f, 0.040f, 0.90f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.035f, 0.045f, 0.060f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.020f, 0.025f, 0.030f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.115f, 0.245f, 0.390f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.160f, 0.350f, 0.565f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.205f, 0.445f, 0.720f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.255f, 0.585f, 0.940f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.255f, 0.565f, 0.900f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.325f, 0.680f, 1.000f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.075f, 0.180f, 0.315f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.110f, 0.285f, 0.500f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.150f, 0.385f, 0.650f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.075f, 0.180f, 0.315f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.110f, 0.285f, 0.500f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.150f, 0.385f, 0.650f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.20f, 0.24f, 0.90f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.20f, 0.42f, 0.68f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.25f, 0.55f, 0.88f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.12f, 0.28f, 0.46f, 0.45f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.18f, 0.42f, 0.68f, 0.75f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 0.58f, 0.92f, 0.95f);
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
    io.IniFilename = "imgui.ini";
    io.LogFilename = nullptr;

    applyStyle();

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

    if (ImGui::GetIO().IniFilename)
        ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);

    Internal::shutdownRendererBackend();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    backend = Internal::Backend::None;
    frame_visible = false;
    ready = false;
}

bool beginFrame()
{
    if (!ready && !init()) return false;

    if (frame_active) ImGui::EndFrame();
    ImGui_ImplGlfw_NewFrame();
    if (backend == Internal::Backend::OpenGL)
        Internal::newFrameOpenGL();
    ImGui::NewFrame();
    frame_active = true;
    frame_visible = Mouse.isGrabbed() == LWCGL_FALSE;
    return frame_visible;
}

bool initialized()
{
    return ready;
}

bool wantsMouse()
{
    return ready && frame_visible && ImGui::GetIO().WantCaptureMouse;
}

bool wantsKeyboard()
{
    return ready && frame_visible && ImGui::GetIO().WantCaptureKeyboard;
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

    const bool visible = frame_visible;
    ImGui::Render();
    frame_active = false;
    frame_visible = false;

    const Backend requested = backendFor(output.api);
    if (requested != backend) {
        if (!activateBackend(requested)) return;
#ifdef __APPLE__
        if (backend == Backend::Metal)
            (void)prepareMetal(output);
#endif
        return;
    }

    if (!visible) return;

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
