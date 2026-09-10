#ifdef __APPLE__

#include "Renderer/Debug/Internal.hpp"

#include <lwmgl/lwmgl.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace Renderer::Debug::Internal {
namespace {

constexpr const char *shader = R"msl(
#include <metal_stdlib>
using namespace metal;

struct DebugVertex {
    float4 position;
    float4 color;
};

struct Uniforms {
    float4x4 mvp;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut debug_vertex(
    const device DebugVertex *vertices [[buffer(0)]],
    constant Uniforms& uniforms [[buffer(1)]],
    uint vertex_id [[vertex_id]])
{
    VertexOut out;
    out.position = uniforms.mvp * vertices[vertex_id].position;
    out.position.z = (out.position.z + out.position.w) * 0.5;
    const float opacity = clamp(vertices[vertex_id].color.a, 0.0, 1.0);
    out.color = float4(vertices[vertex_id].color.rgb * opacity, 1.0);
    return out;
}

fragment float4 debug_fragment(VertexOut in [[stage_in]])
{
    return in.color;
}
)msl";

struct State {
    LWMGLLibrary library = nullptr;
    LWMGLFunction vertex = nullptr;
    LWMGLFunction fragment = nullptr;
    LWMGLRenderPipeline pipeline = nullptr;
    LWMGLBuffer lines = nullptr;
    LWMGLBuffer uniforms = nullptr;
    std::size_t lines_capacity = 0u;
};

State state;

bool resizeBuffer(LWMGLBuffer& buffer, std::size_t& capacity, std::size_t bytes)
{
    if (buffer && capacity >= bytes) return true;
    if (buffer) Metal.destroyBuffer(buffer);
    buffer = nullptr;
    capacity = 0u;

    const LWMGLBufferDesc desc = {bytes, LWMGL_STORAGE_SHARED};
    buffer = Metal.createBuffer(&desc, nullptr);
    if (!buffer) return false;
    capacity = bytes;
    return true;
}

bool init()
{
    if (state.pipeline) return true;
    if (!Metal.isCreated() || !Metal.drawLines) return false;

    state.library = Metal.createLibraryFromSource(shader, std::strlen(shader));
    if (!state.library) return false;
    state.vertex = Metal.createFunction(state.library, "debug_vertex");
    state.fragment = Metal.createFunction(state.library, "debug_fragment");
    if (!state.vertex || !state.fragment) return false;
    state.pipeline = Metal.createRenderPipeline(
        state.vertex,
        state.fragment,
        LWMGL_BGRA8_UNORM
    );
    if (!state.pipeline) return false;

    std::size_t uniform_capacity = 0u;
    return resizeBuffer(state.uniforms, uniform_capacity, sizeof(float) * 16u);
}

} // namespace

void renderMetal(
    const std::vector<Vertex>& lines,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output)
{
    if (!output.command || lines.empty()) return;
    if (!init()) return;
    if (lines.size() > std::numeric_limits<std::uint32_t>::max()) return;

    const std::size_t bytes = lines.size() * sizeof(Vertex);
    if (!resizeBuffer(state.lines, state.lines_capacity, bytes)) return;
    if (Metal.uploadBuffer(state.lines, 0u, lines.data(), bytes) != 0) return;

    const Math::Mat4 mvp = Math::multiply(projection, view);
    if (Metal.uploadBuffer(state.uniforms, 0u, mvp.data(), sizeof(float) * 16u) != 0) return;

    const LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    if (Metal.setRenderPipeline(command, state.pipeline) != 0) return;
    if (Metal.setBuffer(command, state.lines, 0u, 0u) != 0) return;
    if (Metal.setBuffer(command, state.uniforms, 0u, 1u) != 0) return;
    (void)Metal.drawLines(command, 0u, static_cast<std::uint32_t>(lines.size()));
}

void shutdownMetal()
{
    if (state.lines) Metal.destroyBuffer(state.lines);
    if (state.uniforms) Metal.destroyBuffer(state.uniforms);
    if (state.pipeline) Metal.destroyRenderPipeline(state.pipeline);
    if (state.vertex) Metal.destroyFunction(state.vertex);
    if (state.fragment) Metal.destroyFunction(state.fragment);
    if (state.library) Metal.destroyLibrary(state.library);
    state = State{};
}

} // namespace Renderer::Debug::Internal

#endif
