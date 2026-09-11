#ifdef __APPLE__

#include "Renderer/Frame/Metal/FrameMetal.hpp"

#include <lwmgl/lwmgl.h>

#include <cstdio>
#include <cstring>

namespace Renderer::Frame::Metal {
namespace {

inline constexpr const char *source = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct PresentOut {
    float4 position [[position]];
    float2 uv;
};

vertex PresentOut frame_present_vertex(uint id [[vertex_id]])
{
    float2 position = id == 0u ? float2(-1.0f, -1.0f) :
        (id == 1u ? float2(3.0f, -1.0f) : float2(-1.0f, 3.0f));
    PresentOut out;
    out.position = float4(position, 0.0f, 1.0f);
    out.uv = position * 0.5f + 0.5f;
    return out;
}

fragment float4 frame_present_fragment(
    PresentOut in [[stage_in]],
    texture2d<float> color [[texture(0)]],
    sampler color_sampler [[sampler(0)]])
{
    float2 uv = float2(in.uv.x, 1.0f - in.uv.y);
    return float4(max(color.sample(color_sampler, uv).rgb, float3(0.0f)), 1.0f);
}
)MSL";

} // namespace

struct Presenter::Impl {
    LWMGLLibrary library = nullptr;
    LWMGLFunction vertex = nullptr;
    LWMGLFunction fragment = nullptr;
    LWMGLRenderPipeline pipeline = nullptr;
    LWMGLSampler sampler = nullptr;

    bool init()
    {
        if (pipeline && sampler) return true;
        library = ::Metal.createLibraryFromSource(source, std::strlen(source));
        if (!library) return false;
        vertex = ::Metal.createFunction(library, "frame_present_vertex");
        fragment = ::Metal.createFunction(library, "frame_present_fragment");
        if (!vertex || !fragment) return false;
        pipeline = ::Metal.createRenderPipeline(vertex, fragment, LWMGL_BGRA8_UNORM);
        if (!pipeline) return false;

        const LWMGLSamplerDesc desc = {
            LWMGL_FILTER_LINEAR,
            LWMGL_FILTER_LINEAR,
            LWMGL_ADDRESS_CLAMP,
            LWMGL_ADDRESS_CLAMP
        };
        sampler = ::Metal.createSampler(&desc);
        return sampler != nullptr;
    }

    void shutdown()
    {
        if (sampler) ::Metal.destroySampler(sampler);
        if (pipeline) ::Metal.destroyRenderPipeline(pipeline);
        if (vertex) ::Metal.destroyFunction(vertex);
        if (fragment) ::Metal.destroyFunction(fragment);
        if (library) ::Metal.destroyLibrary(library);
        sampler = nullptr;
        pipeline = nullptr;
        vertex = nullptr;
        fragment = nullptr;
        library = nullptr;
    }
};

Presenter::Presenter() : impl_(new Impl) {}

Presenter::~Presenter()
{
    shutdown();
    delete impl_;
}

bool Presenter::init()
{
    return impl_ && impl_->init();
}

bool Presenter::compose(Internal::FrameOutput& output)
{
    if (!output.color_texture) return true;
    if (!impl_ || !impl_->pipeline || !impl_->sampler) return false;

    LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    if (!command) return false;

    const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
    bool ok = ::Metal.beginRenderToDrawable(command, clear, 1) == 0;
    if (ok) ok = ::Metal.setRenderPipeline(command, impl_->pipeline) == 0;
    if (ok) ok = ::Metal.setFragmentTexture(
        command,
        static_cast<LWMGLTexture>(output.color_texture),
        0u
    ) == 0;
    if (ok) ok = ::Metal.setFragmentSampler(command, impl_->sampler, 0u) == 0;
    if (ok) ok = ::Metal.draw(command, 0u, 3u) == 0;
    if (!ok) {
        std::fprintf(stderr, "[Frame/Metal]: composition failed: %s\n", lwmglGetLastError());
    }
    return ok;
}

void Presenter::shutdown()
{
    if (impl_) impl_->shutdown();
}

bool beginClear(Internal::FrameOutput& output)
{
    LWMGLCommand command = ::Metal.begin();
    if (!command) return false;
    const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
    if (::Metal.beginRenderToDrawable(command, clear, 1) != 0) {
        ::Metal.destroyCommand(command);
        return false;
    }
    output.command = command;
    return true;
}

void present(Internal::FrameOutput& output)
{
    LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    if (!command) return;

    bool ok = ::Metal.present(command) == 0;
    if (ok) ok = ::Metal.commit(command) == 0;
    if (ok) ok = ::Metal.wait(command) == 0;
    if (!ok) {
        std::fprintf(stderr, "[Frame/Metal]: presentation failed: %s\n", lwmglGetLastError());
    }

    ::Metal.destroyCommand(command);
    output.command = nullptr;
}

} // namespace Renderer::Frame::Metal

#endif
