#include "Renderer/Debug/Internal.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace Renderer::Debug::Internal {
namespace {

inline constexpr const char *Shader = R"HLSL(
struct DebugVertex {
    float4 position;
    float4 color;
};

StructuredBuffer<DebugVertex> Vertices : register(t0, space0);
cbuffer DebugUniforms : register(b0, space1) {
    float4x4 MVP;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VertexOut VSMain(uint vertex_id : SV_VertexID)
{
    DebugVertex source = Vertices[vertex_id];
    VertexOut result;
    result.position = mul(MVP, source.position);
    result.position.z = (result.position.z + result.position.w) * 0.5f;
    float opacity = saturate(source.color.a);
    result.color = float4(source.color.rgb * opacity, 1.0f);
    return result;
}

float4 PSMain(VertexOut input) : SV_Target0
{
    return input.color;
}
)HLSL";

struct State {
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUBuffer *vertices = nullptr;
    std::size_t capacity = 0u;
};

State state;

bool ensurePipeline()
{
    if (state.pipeline) return true;
    if (!Renderer::SDLGPU::device()) return false;

    SDL_GPUShader *vertex = Renderer::SDLGPU::compileGraphicsShader(
        Shader, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
        "Horse Debug VS", "VSMain");
    SDL_GPUShader *fragment = Renderer::SDLGPU::compileGraphicsShader(
        Shader, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
        "Horse Debug PS", "PSMain");
    if (!vertex || !fragment) {
        if (vertex) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), vertex);
        if (fragment) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), fragment);
        return false;
    }

    SDL_GPUColorTargetDescription color{};
    color.format = Renderer::SDLGPU::colorFormat();

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1u;
    state.pipeline = SDL_CreateGPUGraphicsPipeline(Renderer::SDLGPU::device(), &info);

    SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), fragment);
    SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), vertex);
    if (!state.pipeline)
        std::fprintf(stderr, "[Debug/SDL_GPU]: pipeline creation failed: %s\n", SDL_GetError());
    return state.pipeline != nullptr;
}

bool ensureVertexBuffer(std::size_t bytes)
{
    if (state.vertices && state.capacity >= bytes) return true;
    if (state.vertices)
        SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), state.vertices);
    state.vertices = nullptr;
    state.capacity = 0u;

    const std::size_t capacity = std::max<std::size_t>(bytes, sizeof(Vertex) * 256u);
    state.vertices = Renderer::SDLGPU::createBuffer(
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        capacity,
        nullptr,
        "Horse Debug Vertices");
    if (!state.vertices) return false;
    state.capacity = capacity;
    return true;
}

} // namespace

void renderSDLGPU(
    const std::vector<Vertex>& lines,
    const Math::Mat4& projection,
    const Math::Mat4& view,
    Renderer::Internal::FrameOutput& output)
{
    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    auto *color = static_cast<SDL_GPUTexture *>(output.color_texture);
    if (!command || !color || lines.empty()) return;
    if (lines.size() > std::numeric_limits<Uint32>::max()) return;
    if (!ensurePipeline()) return;

    const std::size_t bytes = lines.size() * sizeof(Vertex);
    if (!ensureVertexBuffer(bytes) ||
        !Renderer::SDLGPU::uploadBuffer(command, state.vertices, lines.data(), bytes, true))
    {
        std::fprintf(stderr, "[Debug/SDL_GPU]: vertex upload failed: %s\n", SDL_GetError());
        return;
    }

    const Math::Mat4 mvp = Math::multiply(projection, view);
    SDL_PushGPUVertexUniformData(command, 0u, mvp.data(), sizeof(float) * 16u);

    SDL_GPUColorTargetInfo target{};
    target.texture = color;
    target.load_op = SDL_GPU_LOADOP_LOAD;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &target, 1u, nullptr);
    if (!pass) {
        std::fprintf(stderr, "[Debug/SDL_GPU]: render pass failed: %s\n", SDL_GetError());
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, state.pipeline);
    SDL_GPUBuffer *buffers[] = {state.vertices};
    SDL_BindGPUVertexStorageBuffers(pass, 0u, buffers, 1u);
    SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(lines.size()), 1u, 0u, 0u);
    SDL_EndGPURenderPass(pass);
}

void shutdownSDLGPU()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (state.vertices) SDL_ReleaseGPUBuffer(device, state.vertices);
        if (state.pipeline) SDL_ReleaseGPUGraphicsPipeline(device, state.pipeline);
    }
    state = {};
}

} // namespace Renderer::Debug::Internal
