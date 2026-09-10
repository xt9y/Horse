#ifdef __APPLE__

#include "Renderer/Debug/Internal.hpp"

#include <lwmgl/lwmgl.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Renderer::Debug::Internal {
namespace {

constexpr const char *kShader = R"msl(
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
    LWMGLBuffer wireframe = nullptr;
    LWMGLBuffer dynamic = nullptr;
    LWMGLBuffer uniforms = nullptr;
    std::size_t wireframe_capacity = 0u;
    std::size_t dynamic_capacity = 0u;
    std::uint64_t wireframe_revision = 0u;
};

State state;

bool createBuffer(LWMGLBuffer& buffer, std::size_t& capacity, std::size_t bytes)
{
    const std::size_t required = std::max<std::size_t>(bytes, 16u);
    if (buffer && capacity >= required) return true;
    if (buffer) Metal.destroyBuffer(buffer);
    buffer = nullptr;
    capacity = 0u;

    const LWMGLBufferDesc desc = {required, LWMGL_STORAGE_SHARED};
    buffer = Metal.createBuffer(&desc, nullptr);
    if (!buffer) return false;
    capacity = required;
    return true;
}

bool init()
{
    if (state.pipeline) return true;
    if (!Metal.isCreated()) return false;

    state.library = Metal.createLibraryFromSource(kShader, std::strlen(kShader));
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
    return createBuffer(state.uniforms, uniform_capacity, sizeof(float) * 16u);
}

bool upload(
    LWMGLBuffer& buffer,
    std::size_t& capacity,
    const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return true;
    const std::size_t bytes = vertices.size() * sizeof(Vertex);
    if (!createBuffer(buffer, capacity, bytes)) return false;
    return Metal.uploadBuffer(buffer, 0u, vertices.data(), bytes) == 0;
}

void draw(LWMGLCommand command, LWMGLBuffer buffer, std::size_t count)
{
    if (!buffer || count == 0u || !Metal.nativeRenderEncoder) return;
    if (Metal.setBuffer(command, buffer, 0u, 0u) != 0) return;

    void *native = Metal.nativeRenderEncoder(command);
    if (!native) return;
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)native;
    [encoder drawPrimitives:MTLPrimitiveTypeLine
                vertexStart:0
                vertexCount:static_cast<NSUInteger>(count)];
}

} // namespace

void renderMetal(
    const std::vector<Vertex>& wireframe,
    std::uint64_t wireframe_revision,
    const std::vector<Vertex>& dynamic,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output)
{
    if (!output.command || (wireframe.empty() && dynamic.empty())) return;
    if (!init()) return;

    const LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    if (wireframe_revision != state.wireframe_revision) {
        if (!upload(state.wireframe, state.wireframe_capacity, wireframe)) return;
        state.wireframe_revision = wireframe_revision;
    }
    if (!upload(state.dynamic, state.dynamic_capacity, dynamic)) return;

    const Math::Mat4 mvp = Math::multiply(projection, view);
    if (Metal.uploadBuffer(state.uniforms, 0u, mvp.data(), sizeof(float) * 16u) != 0) return;
    if (Metal.setRenderPipeline(command, state.pipeline) != 0) return;
    if (Metal.setBuffer(command, state.uniforms, 0u, 1u) != 0) return;

    draw(command, state.wireframe, wireframe.size());
    draw(command, state.dynamic, dynamic.size());
}

void shutdownMetal()
{
    if (state.wireframe) Metal.destroyBuffer(state.wireframe);
    if (state.dynamic) Metal.destroyBuffer(state.dynamic);
    if (state.uniforms) Metal.destroyBuffer(state.uniforms);
    if (state.pipeline) Metal.destroyRenderPipeline(state.pipeline);
    if (state.vertex) Metal.destroyFunction(state.vertex);
    if (state.fragment) Metal.destroyFunction(state.fragment);
    if (state.library) Metal.destroyLibrary(state.library);
    state = State{};
}

} // namespace Renderer::Debug::Internal

#endif
