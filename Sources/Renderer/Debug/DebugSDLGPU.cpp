#include "Renderer/Internal/DebugRenderPass.hpp"

#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace Renderer::Debug::RenderPass {
namespace {

inline constexpr const char *Shader = R"HLSL(
struct DebugVertex {
    float4 position;
    float4 color;
};

StructuredBuffer<DebugVertex> Vertices : register(t0, space0);

cbuffer Frame : register(b0, space1) {
    float4 CameraPositionNear;
    float4 CameraForwardFar;
    float4 CameraRightAspect;
    float4 CameraUpTanHalfFov;
    float4 ProjectionAlpha;
    float4 Resolution;
    int4 Counts;
    uint4 FrameData;
    uint4 PathPolicy;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VertexOut VSMain(uint vertex_id : SV_VertexID)
{
    DebugVertex source = Vertices[vertex_id];
    float3 delta = source.position.xyz - CameraPositionNear.xyz;
    float depth = dot(delta, CameraForwardFar.xyz);

    VertexOut result;
    if (ProjectionAlpha.z > 0.5) {
        float x = dot(delta, CameraRightAspect.xyz) / max(ProjectionAlpha.x, 1.0e-6);
        float y = dot(delta, CameraUpTanHalfFov.xyz) / max(ProjectionAlpha.y, 1.0e-6);
        float z = saturate((depth - CameraPositionNear.w) /
            max(CameraForwardFar.w - CameraPositionNear.w, 1.0e-5));
        result.position = float4(x, y, z, 1.0);
    } else {
        float x = dot(delta, CameraRightAspect.xyz) /
            max(CameraUpTanHalfFov.w * CameraRightAspect.w, 1.0e-6);
        float y = dot(delta, CameraUpTanHalfFov.xyz) /
            max(CameraUpTanHalfFov.w, 1.0e-6);
        float near_z = CameraPositionNear.w;
        float far_z = CameraForwardFar.w;
        float z = far_z < 3.0e37
            ? (far_z * depth - near_z * far_z) / max(far_z - near_z, 1.0e-5)
            : depth - near_z;
        result.position = float4(x, y, z, depth);
    }

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
    const Scenes::Scene::CameraState& camera,
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

    const Scenes::CameraState raster_camera = Scenes::cameraState(camera);
    if (!raster_camera.valid) return;

    const SDLGPU::FrameUniforms uniforms = SDLGPU::makeFrameUniforms(
        raster_camera,
        output.width,
        output.height,
        output.width,
        output.height,
        0u,
        0u,
        0u,
        0u,
        0.0f
    );
    SDL_PushGPUVertexUniformData(command, 0u, &uniforms, sizeof(uniforms));

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

} // namespace Renderer::Debug::RenderPass
