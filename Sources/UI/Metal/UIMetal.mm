#ifdef __APPLE__

#include "UI/Internal.hpp"

#include <imgui.h>
#include <backends/imgui_impl_metal.h>
#include <lwmgl/lwmgl.h>

#import <Metal/Metal.h>

namespace UI::Internal {

bool initMetal()
{
    if (!Metal.isCreated() || !Metal.nativeDevice) return false;
    void *native_device = Metal.nativeDevice();
    if (!native_device) return false;
    return ImGui_ImplMetal_Init((__bridge id<MTLDevice>)native_device);
}

void shutdownMetal()
{
    ImGui_ImplMetal_Shutdown();
}

bool renderMetal(
    ImDrawData *draw_data,
    Renderer::Internal::FrameOutput& output)
{
    if (!draw_data || !output.command ||
        !Metal.nativeCommandBuffer ||
        !Metal.nativeRenderEncoder ||
        !Metal.nativeRenderPassDescriptor)
    {
        return false;
    }

    const LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    void *native_command = Metal.nativeCommandBuffer(command);
    void *native_encoder = Metal.nativeRenderEncoder(command);
    void *native_pass = Metal.nativeRenderPassDescriptor(command);
    if (!native_command || !native_encoder || !native_pass) return false;

    ImGui_ImplMetal_NewFrame((__bridge MTLRenderPassDescriptor *)native_pass);
    ImGui_ImplMetal_RenderDrawData(
        draw_data,
        (__bridge id<MTLCommandBuffer>)native_command,
        (__bridge id<MTLRenderCommandEncoder>)native_encoder
    );
    return true;
}

} // namespace UI::Internal

#endif
