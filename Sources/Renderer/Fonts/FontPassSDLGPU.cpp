#include "Renderer/Fonts/FontPass.hpp"

#include "Font.hpp"
#include "Models/Images/Image.hpp"
#include "Renderer/Fonts/FontAtlas.hpp"
#include "Renderer/SDLGPU/Context.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace Renderer::Internal {
namespace {

struct alignas(16) FontUniforms {
    float output_width = 1.0f;
    float output_height = 1.0f;
    float has_linear_depth = 0.0f;
    float padding = 0.0f;
};

inline constexpr const char *Shader = R"HLSL(
struct FontVertex {
    float4 clip;
    float4 uv_depth;
    float4 color;
};

StructuredBuffer<FontVertex> Vertices : register(t0, space0);

Texture2D<float4> Atlas : register(t0, space2);
SamplerState AtlasSampler : register(s0, space2);
Texture2D<float4> SceneDepth : register(t1, space2);
SamplerState DepthSampler : register(s1, space2);

cbuffer FontUniforms : register(b0, space3) {
    float OutputWidth;
    float OutputHeight;
    float HasLinearDepth;
    float Padding;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float linear_depth : TEXCOORD1;
    float depth_test : TEXCOORD2;
    float4 color : COLOR0;
};

VertexOut VSMain(uint vertex_id : SV_VertexID)
{
    FontVertex source = Vertices[vertex_id];
    VertexOut result;
    result.position = source.clip;
    result.position.z = (source.clip.z + source.clip.w) * 0.5f;
    result.uv = source.uv_depth.xy;
    result.linear_depth = source.uv_depth.z;
    result.depth_test = source.uv_depth.w;
    result.color = source.color;
    return result;
}

float4 PSMain(VertexOut input) : SV_Target0
{
    float4 texel = Atlas.Sample(AtlasSampler, input.uv);
    float alpha = texel.a * input.color.a;
    if (alpha < 0.5f) discard;

    if (input.depth_test > 0.5f && HasLinearDepth > 0.5f) {
        float2 output_size = max(float2(OutputWidth, OutputHeight), 1.0.xx);
        float2 screen_uv = clamp(input.position.xy / output_size, 0.0.xx, 0.999999.xx);
        float surface_depth = SceneDepth.Sample(DepthSampler, screen_uv).r;
        float epsilon = max(0.0025f, surface_depth * 0.0005f);
        if (input.linear_depth > surface_depth + epsilon) discard;
    }

    return float4(input.color.rgb * texel.rgb, alpha);
}
)HLSL";

struct State {
    SDL_GPUGraphicsPipeline *overlay_pipeline = nullptr;
    SDL_GPUGraphicsPipeline *native_depth_pipeline = nullptr;
    SDL_GPUTexture *atlas = nullptr;
    SDL_GPUTexture *empty_depth = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    SDL_GPUBuffer *vertices = nullptr;
    std::size_t vertex_capacity = 0u;
};

State state;

SDL_GPUGraphicsPipeline *createPipeline(
    SDL_GPUShader *vertex,
    SDL_GPUShader *fragment,
    bool native_depth)
{
    SDL_GPUColorTargetDescription color{};
    color.format = Renderer::SDLGPU::colorFormat();
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.enable_blend = true;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    if (native_depth) {
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = false;
        info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.target_info.has_depth_stencil_target = true;
    }
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1u;
    return SDL_CreateGPUGraphicsPipeline(Renderer::SDLGPU::device(), &info);
}

bool createAtlas()
{
    Models::Images::Image image;
    std::string error;
    const Font::AtlasSettings& atlas = Font::atlas();
    const bool loaded = !atlas.path.empty()
        && Models::Images::load(atlas.path, &image, &error)
        && image.width > 0
        && image.height > 0
        && image.rgba.size() == static_cast<std::size_t>(image.width) *
            static_cast<std::size_t>(image.height) * 4u;

    if (!loaded) {
        image.width = FontAtlas::FALLBACK_WIDTH;
        image.height = FontAtlas::FALLBACK_HEIGHT;
        image.rgba = FontAtlas::rgba();
        image.meaningful_alpha = true;
    }

    state.atlas = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        static_cast<std::uint32_t>(image.width),
        static_cast<std::uint32_t>(image.height),
        "Horse Font Atlas");
    return state.atlas && Renderer::SDLGPU::uploadTextureRgba8(
        state.atlas,
        static_cast<std::uint32_t>(image.width),
        static_cast<std::uint32_t>(image.height),
        image.rgba.data(),
        image.rgba.size());
}

bool createEmptyDepth()
{
    const std::uint8_t white[4] = {255u, 255u, 255u, 255u};
    state.empty_depth = Renderer::SDLGPU::createTexture(
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        1u, 1u,
        "Horse Font Empty Depth");
    return state.empty_depth && Renderer::SDLGPU::uploadTextureRgba8(
        state.empty_depth, 1u, 1u, white, sizeof white);
}

bool ensureState()
{
    if (state.overlay_pipeline && state.native_depth_pipeline && state.atlas &&
        state.empty_depth && state.sampler)
        return true;
    if (!Renderer::SDLGPU::device()) return false;

    SDL_GPUShader *vertex = Renderer::SDLGPU::compileGraphicsShader(
        Shader, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
        "Horse Font VS", "VSMain");
    SDL_GPUShader *fragment = Renderer::SDLGPU::compileGraphicsShader(
        Shader, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
        "Horse Font PS", "PSMain");
    if (!vertex || !fragment) {
        if (vertex) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), vertex);
        if (fragment) SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), fragment);
        return false;
    }

    state.overlay_pipeline = createPipeline(vertex, fragment, false);
    state.native_depth_pipeline = createPipeline(vertex, fragment, true);
    SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), fragment);
    SDL_ReleaseGPUShader(Renderer::SDLGPU::device(), vertex);

    state.sampler = Renderer::SDLGPU::createNearestSampler();
    if (!state.overlay_pipeline || !state.native_depth_pipeline || !state.sampler ||
        !createAtlas() || !createEmptyDepth())
    {
        std::fprintf(stderr, "[Font/SDL_GPU]: initialization failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

bool ensureVertexBuffer(std::size_t bytes)
{
    if (state.vertices && state.vertex_capacity >= bytes) return true;
    if (state.vertices)
        SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), state.vertices);
    state.vertices = nullptr;
    state.vertex_capacity = 0u;

    const std::size_t capacity = std::max<std::size_t>(bytes, sizeof(FontVertex) * 256u);
    state.vertices = Renderer::SDLGPU::createBuffer(
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        capacity,
        nullptr,
        "Horse Font Vertices");
    if (!state.vertices) return false;
    state.vertex_capacity = capacity;
    return true;
}

bool drawBatch(
    const std::vector<FontVertex>& vertices,
    FrameOutput& output,
    SDL_GPUGraphicsPipeline *pipeline,
    SDL_GPUTexture *sampled_depth,
    bool has_linear_depth,
    SDL_GPUTexture *native_depth)
{
    if (vertices.empty()) return true;
    if (vertices.size() > std::numeric_limits<Uint32>::max()) return false;
    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    auto *color = static_cast<SDL_GPUTexture *>(output.color_texture);
    if (!command || !color || !pipeline) return false;

    const std::size_t bytes = vertices.size() * sizeof(FontVertex);
    if (!ensureVertexBuffer(bytes) ||
        !Renderer::SDLGPU::uploadBuffer(command, state.vertices, vertices.data(), bytes, true))
        return false;

    const FontUniforms uniforms{
        static_cast<float>(std::max(output.width, 1)),
        static_cast<float>(std::max(output.height, 1)),
        has_linear_depth ? 1.0f : 0.0f,
        0.0f,
    };
    SDL_PushGPUFragmentUniformData(command, 0u, &uniforms, sizeof uniforms);

    SDL_GPUColorTargetInfo color_target{};
    color_target.texture = color;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_target{};
    SDL_GPUDepthStencilTargetInfo *depth_info = nullptr;
    if (native_depth) {
        depth_target.texture = native_depth;
        depth_target.load_op = SDL_GPU_LOADOP_LOAD;
        depth_target.store_op = SDL_GPU_STOREOP_STORE;
        depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_info = &depth_target;
    }

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &color_target, 1u, depth_info);
    if (!pass) return false;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUBuffer *storage[] = {state.vertices};
    SDL_BindGPUVertexStorageBuffers(pass, 0u, storage, 1u);
    const SDL_GPUTextureSamplerBinding samplers[2] = {
        {state.atlas, state.sampler},
        {sampled_depth ? sampled_depth : state.empty_depth, state.sampler},
    };
    SDL_BindGPUFragmentSamplers(pass, 0u, samplers, 2u);
    SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(vertices.size()), 1u, 0u, 0u);
    SDL_EndGPURenderPass(pass);
    return true;
}

} // namespace

void renderFontsSDLGPU(const FontBatches& batches, FrameOutput& output)
{
    if (!output.command || !output.color_texture || !ensureState()) return;

    const bool linear_depth = output.depth == DepthSource::LinearTexture && output.depth_texture;
    const bool native_depth = output.depth == DepthSource::Native && output.depth_texture;

    if (!batches.depth.empty()) {
        const bool ok = native_depth
            ? drawBatch(
                batches.depth, output, state.native_depth_pipeline,
                state.empty_depth, false,
                static_cast<SDL_GPUTexture *>(output.depth_texture))
            : linear_depth && drawBatch(
                batches.depth, output, state.overlay_pipeline,
                static_cast<SDL_GPUTexture *>(output.depth_texture), true, nullptr);
        if (!ok && (native_depth || linear_depth))
            std::fprintf(stderr, "[Font/SDL_GPU]: depth text draw failed: %s\n", SDL_GetError());
    }

    if (!batches.overlay.empty() && !drawBatch(
            batches.overlay, output, state.overlay_pipeline,
            state.empty_depth, false, nullptr))
    {
        std::fprintf(stderr, "[Font/SDL_GPU]: overlay draw failed: %s\n", SDL_GetError());
    }
}

void shutdownFontsSDLGPU()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (state.vertices) SDL_ReleaseGPUBuffer(device, state.vertices);
        if (state.sampler) SDL_ReleaseGPUSampler(device, state.sampler);
        if (state.empty_depth) SDL_ReleaseGPUTexture(device, state.empty_depth);
        if (state.atlas) SDL_ReleaseGPUTexture(device, state.atlas);
        if (state.native_depth_pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, state.native_depth_pipeline);
        if (state.overlay_pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, state.overlay_pipeline);
    }
    state = {};
}

} // namespace Renderer::Internal
