#include "UI/UI.hpp"

#include "Input.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "UI/Internal.hpp"
#include "Window/Internal.hpp"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <imgui.h>

namespace UI {
namespace {

bool ready = false;
bool frame_active = false;
bool frame_visible = false;

} // namespace

bool init()
{
    if (ready) return true;

    SDL_Window *window = Window::Internal::window();
    if (!window || !Renderer::SDLGPU::retain()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        ImGui::DestroyContext();
        Renderer::SDLGPU::release();
        return false;
    }

    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = Renderer::SDLGPU::device();
    info.ColorTargetFormat = Renderer::SDLGPU::colorFormat();
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        Renderer::SDLGPU::release();
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

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    Renderer::SDLGPU::release();

    frame_visible = false;
    ready = false;
}

bool beginFrame()
{
    if (!ready && !init()) return false;

    if (frame_active) ImGui::EndFrame();

    for (const SDL_Event& event : Window::Internal::events())
        ImGui_ImplSDL3_ProcessEvent(&event);

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    frame_active = true;
    frame_visible = !Input::pointer().captured;
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

void render(Renderer::Internal::FrameOutput& output)
{
    if (!ready || !frame_active) return;

    const bool visible = frame_visible;
    ImGui::Render();
    frame_active = false;
    frame_visible = false;

    if (!visible || output.api != Renderer::Internal::GraphicsApi::SDLGPU) return;

    ImDrawData *draw_data = ImGui::GetDrawData();
    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    auto *color = static_cast<SDL_GPUTexture *>(output.color_texture);
    if (!draw_data || !command || !color) return;

    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command);

    SDL_GPUColorTargetInfo color_target{};
    color_target.texture = color;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &color_target, 1u, nullptr);
    if (!pass) return;
    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command, pass);
    SDL_EndGPURenderPass(pass);
}

} // namespace Internal
} // namespace UI
