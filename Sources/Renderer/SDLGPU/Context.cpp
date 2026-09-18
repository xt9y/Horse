#include "Renderer/SDLGPU/Context.hpp"

#include "Window/Internal/Backend.hpp"

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Renderer::SDLGPU {
namespace {

inline constexpr const char *ColorTransformShader = R"HLSL(
Texture2D<float4> Source : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
RWTexture2D<float4> Output : register(u0, space1);
cbuffer TransformData : register(b0, space2) {
    float ExposureEV;
    uint ToneMapping;
    uint EncodeSrgb;
    uint Width;
    uint Height;
    uint3 Padding;
};

float3 Aces(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 LinearToSrgb(float3 color)
{
    color = max(color, 0.0.xxx);
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return lerp(low, high, step(0.0031308.xxx, color));
}

[numthreads(8, 8, 1)]
void Main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= Width || tid.y >= Height) return;
    float2 uv = (float2(tid.xy) + 0.5.xx) / float2(Width, Height);
    float4 source = Source.SampleLevel(SourceSampler, uv, 0.0);
    float3 color = max(source.rgb, 0.0.xxx) * exp2(ExposureEV);
    if (ToneMapping == 1u) color = Aces(color);
    else color = saturate(color);
    if (EncodeSrgb != 0u) color = LinearToSrgb(color);
    Output[tid.xy] = float4(color, source.a);
}
)HLSL";

struct alignas(16) ColorTransformData {
    float exposure_ev = 0.0f;
    std::uint32_t tone_mapping = 0u;
    std::uint32_t encode_srgb = 0u;
    std::uint32_t width = 1u;
    std::uint32_t height = 1u;
    std::uint32_t padding[3]{};
};

static_assert(sizeof(ColorTransformData) == 32u);

struct State {
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTextureFormat swapchain_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSwapchainComposition composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    SDL_GPUComputePipeline *color_transform = nullptr;
    SDL_GPUSampler *color_sampler = nullptr;
    std::uint32_t references = 0u;
    bool shadercross = false;
};

State& state()
{
    static State value;
    return value;
}

bool createDevice()
{
    State& value = state();
    SDL_Window *window = Window::Internal::window();
    if (!window) {
        std::fprintf(stderr, "[SDL_GPU]: Window::create() must be called before renderer initialization\n");
        return false;
    }

    if (!SDL_ShaderCross_Init()) {
        std::fprintf(stderr, "[SDL_GPU]: SDL_shadercross initialization failed: %s\n", SDL_GetError());
        return false;
    }
    value.shadercross = true;

    const SDL_GPUShaderFormat formats = SDL_ShaderCross_GetSPIRVShaderFormats();
    if (formats == SDL_GPU_SHADERFORMAT_INVALID) {
        std::fprintf(stderr, "[SDL_GPU]: SDL_shadercross reports no usable shader formats\n");
        SDL_ShaderCross_Quit();
        value.shadercross = false;
        return false;
    }

    if (SDL_GPUSupportsShaderFormats(formats, "vulkan"))
        value.device = SDL_CreateGPUDevice(formats, false, "vulkan");
    if (!value.device)
        value.device = SDL_CreateGPUDevice(formats, false, nullptr);
    if (!value.device) {
        std::fprintf(stderr, "[SDL_GPU]: device creation failed: %s\n", SDL_GetError());
        SDL_ShaderCross_Quit();
        value.shadercross = false;
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(value.device, window)) {
        std::fprintf(stderr, "[SDL_GPU]: window claim failed: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(value.device);
        value.device = nullptr;
        SDL_ShaderCross_Quit();
        value.shadercross = false;
        return false;
    }

    value.composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    if (SDL_WindowSupportsGPUSwapchainComposition(
            value.device,
            window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR) &&
        SDL_SetGPUSwapchainParameters(
            value.device,
            window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
            SDL_GPU_PRESENTMODE_VSYNC))
    {
        value.composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR;
    } else {
        SDL_SetGPUSwapchainParameters(
            value.device,
            window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            SDL_GPU_PRESENTMODE_VSYNC);
    }

    value.swapchain_format = SDL_GetGPUSwapchainTextureFormat(value.device, window);
    SDL_SetGPUAllowedFramesInFlight(value.device, 2u);
    std::fprintf(
        stderr,
        "[SDL_GPU]: %s backend active%s, %s swapchain\n",
        SDL_GetGPUDeviceDriver(value.device),
        std::strcmp(SDL_GetGPUDeviceDriver(value.device), "vulkan") == 0
            ? " (preferred)"
            : " (Vulkan unavailable; fallback)",
        value.composition == SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR
            ? "SDR_LINEAR"
            : "SDR"
    );
    return true;
}

void destroyDevice()
{
    State& value = state();
    if (value.device) {
        SDL_WaitForGPUIdle(value.device);
        if (value.color_sampler) SDL_ReleaseGPUSampler(value.device, value.color_sampler);
        if (value.color_transform) SDL_ReleaseGPUComputePipeline(value.device, value.color_transform);
        value.color_sampler = nullptr;
        value.color_transform = nullptr;
        if (SDL_Window *window = Window::Internal::window())
            SDL_ReleaseWindowFromGPUDevice(value.device, window);
        SDL_DestroyGPUDevice(value.device);
    }
    value.device = nullptr;
    value.swapchain_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    value.composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    if (value.shadercross) SDL_ShaderCross_Quit();
    value.shadercross = false;
}

SDL_ShaderCross_ShaderStage stageFor(SDL_ShaderCross_ShaderStage stage)
{
    return stage;
}

bool ensureColorTransform()
{
    State& value = state();
    if (!value.device) return false;
    if (!value.color_transform)
        value.color_transform = compileComputePipeline(
            ColorTransformShader,
            "Horse Color Transform",
            "Main");
    if (!value.color_sampler) value.color_sampler = createLinearSampler();
    return value.color_transform && value.color_sampler;
}

} // namespace

bool retain()
{
    State& value = state();
    if (!value.device && !createDevice()) return false;
    ++value.references;
    return true;
}

void release()
{
    State& value = state();
    if (value.references > 0u) --value.references;
    if (value.references == 0u) destroyDevice();
}

bool initialized() { return state().device != nullptr; }
SDL_GPUDevice *device() { return state().device; }
SDL_GPUTextureFormat swapchainFormat() { return state().swapchain_format; }
SDL_GPUTextureFormat colorFormat() { return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; }
const char *driver() { return state().device ? SDL_GetGPUDeviceDriver(state().device) : "none"; }
bool linearSwapchain() { return state().composition == SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR; }

SDL_GPUShader *compileGraphicsShader(
    const char *source,
    SDL_ShaderCross_ShaderStage stage,
    const char *label,
    const char *entrypoint)
{
    if (!state().device || !source || !entrypoint) return nullptr;
    if (stage == SDL_SHADERCROSS_SHADERSTAGE_COMPUTE) return nullptr;

    SDL_ShaderCross_HLSL_Info hlsl{};
    hlsl.source = source;
    hlsl.entrypoint = entrypoint;
    hlsl.shader_stage = stageFor(stage);

    std::size_t bytecode_size = 0u;
    void *bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl, &bytecode_size);
    if (!bytecode || bytecode_size == 0u) {
        std::fprintf(stderr, "[%s]: HLSL compilation failed: %s\n", label ? label : "Shader", SDL_GetError());
        if (bytecode) SDL_free(bytecode);
        return nullptr;
    }

    SDL_ShaderCross_GraphicsShaderMetadata *metadata =
        SDL_ShaderCross_ReflectGraphicsSPIRV(static_cast<const Uint8 *>(bytecode), bytecode_size, 0);
    if (!metadata) {
        std::fprintf(stderr, "[%s]: SPIR-V reflection failed: %s\n", label ? label : "Shader", SDL_GetError());
        SDL_free(bytecode);
        return nullptr;
    }

    SDL_ShaderCross_SPIRV_Info spirv{};
    spirv.bytecode = static_cast<const Uint8 *>(bytecode);
    spirv.bytecode_size = bytecode_size;
    spirv.entrypoint = entrypoint;
    spirv.shader_stage = stage;

    SDL_PropertiesID properties = SDL_CreateProperties();
    if (label) SDL_SetStringProperty(properties, SDL_PROP_GPU_SHADER_CREATE_NAME_STRING, label);
    SDL_GPUShader *shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        state().device,
        &spirv,
        &metadata->resource_info,
        properties
    );
    if (properties) SDL_DestroyProperties(properties);
    SDL_free(metadata);
    SDL_free(bytecode);
    if (!shader)
        std::fprintf(stderr, "[%s]: SDL_GPU shader creation failed: %s\n", label ? label : "Shader", SDL_GetError());
    return shader;
}

SDL_GPUComputePipeline *compileComputePipeline(
    const char *source,
    const char *label,
    const char *entrypoint)
{
    if (!state().device || !source || !entrypoint) return nullptr;

    SDL_ShaderCross_HLSL_Info hlsl{};
    hlsl.source = source;
    hlsl.entrypoint = entrypoint;
    hlsl.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;

    std::size_t bytecode_size = 0u;
    void *bytecode = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl, &bytecode_size);
    if (!bytecode || bytecode_size == 0u) {
        std::fprintf(stderr, "[%s]: compute HLSL compilation failed: %s\n", label ? label : "Compute", SDL_GetError());
        if (bytecode) SDL_free(bytecode);
        return nullptr;
    }

    SDL_ShaderCross_ComputePipelineMetadata *metadata =
        SDL_ShaderCross_ReflectComputeSPIRV(static_cast<const Uint8 *>(bytecode), bytecode_size, 0);
    if (!metadata) {
        std::fprintf(stderr, "[%s]: compute SPIR-V reflection failed: %s\n", label ? label : "Compute", SDL_GetError());
        SDL_free(bytecode);
        return nullptr;
    }

    SDL_ShaderCross_SPIRV_Info spirv{};
    spirv.bytecode = static_cast<const Uint8 *>(bytecode);
    spirv.bytecode_size = bytecode_size;
    spirv.entrypoint = entrypoint;
    spirv.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;

    SDL_PropertiesID properties = SDL_CreateProperties();
    if (label) SDL_SetStringProperty(properties, SDL_PROP_GPU_COMPUTEPIPELINE_CREATE_NAME_STRING, label);
    SDL_GPUComputePipeline *pipeline = SDL_ShaderCross_CompileComputePipelineFromSPIRV(
        state().device,
        &spirv,
        metadata,
        properties
    );
    if (properties) SDL_DestroyProperties(properties);
    SDL_free(metadata);
    SDL_free(bytecode);
    if (!pipeline)
        std::fprintf(stderr, "[%s]: SDL_GPU compute pipeline creation failed: %s\n", label ? label : "Compute", SDL_GetError());
    return pipeline;
}

SDL_GPUBuffer *createBuffer(
    SDL_GPUBufferUsageFlags usage,
    std::size_t size,
    const void *data,
    const char *label)
{
    if (!state().device || size == 0u || size > std::numeric_limits<Uint32>::max()) return nullptr;
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = static_cast<Uint32>(size);
    SDL_PropertiesID properties = 0;
    if (label) {
        properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_GPU_BUFFER_CREATE_NAME_STRING, label);
        info.props = properties;
    }
    SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(state().device, &info);
    if (properties) SDL_DestroyProperties(properties);
    if (!buffer) return nullptr;
    if (!data) return buffer;

    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(state().device);
    if (!command || !uploadBuffer(command, buffer, data, size, false) ||
        !SDL_SubmitGPUCommandBuffer(command))
    {
        if (command) SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUBuffer(state().device, buffer);
        return nullptr;
    }
    return buffer;
}

bool uploadBuffer(
    SDL_GPUCommandBuffer *command,
    SDL_GPUBuffer *buffer,
    const void *data,
    std::size_t size,
    bool cycle)
{
    if (!state().device || !command || !buffer || !data || size == 0u ||
        size > std::numeric_limits<Uint32>::max()) return false;

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(size);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(state().device, &transfer_info);
    if (!transfer) return false;
    void *mapped = SDL_MapGPUTransferBuffer(state().device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(state().device, transfer);
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(state().device, transfer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        SDL_ReleaseGPUTransferBuffer(state().device, transfer);
        return false;
    }
    const SDL_GPUTransferBufferLocation source{transfer, 0u};
    const SDL_GPUBufferRegion destination{buffer, 0u, static_cast<Uint32>(size)};
    SDL_UploadToGPUBuffer(copy, &source, &destination, cycle);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(state().device, transfer);
    return true;
}

SDL_GPUTexture *createTexture(
    SDL_GPUTextureFormat format,
    SDL_GPUTextureUsageFlags usage,
    std::uint32_t width,
    std::uint32_t height,
    const char *label)
{
    if (!state().device || width == 0u || height == 0u) return nullptr;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1u;
    info.num_levels = 1u;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_PropertiesID properties = 0;
    if (label) {
        properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, label);
        info.props = properties;
    }
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(state().device, &info);
    if (properties) SDL_DestroyProperties(properties);
    return texture;
}

bool uploadTextureRgba8(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *texture,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size)
{
    if (!state().device || !command || !texture || !rgba || width == 0u || height == 0u ||
        size < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u ||
        size > std::numeric_limits<Uint32>::max()) return false;

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = static_cast<Uint32>(size);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(state().device, &transfer_info);
    if (!transfer) return false;
    void *mapped = SDL_MapGPUTransferBuffer(state().device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(state().device, transfer);
        return false;
    }
    std::memcpy(mapped, rgba, size);
    SDL_UnmapGPUTransferBuffer(state().device, transfer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(command);
    if (!copy) {
        SDL_ReleaseGPUTransferBuffer(state().device, transfer);
        return false;
    }
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.offset = 0u;
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1u;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(state().device, transfer);
    return true;
}

bool uploadTextureRgba8(
    SDL_GPUTexture *texture,
    std::uint32_t width,
    std::uint32_t height,
    const void *rgba,
    std::size_t size)
{
    if (!state().device) return false;
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(state().device);
    if (!command) return false;
    if (!uploadTextureRgba8(command, texture, width, height, rgba, size)) {
        SDL_CancelGPUCommandBuffer(command);
        return false;
    }
    return SDL_SubmitGPUCommandBuffer(command);
}

SDL_GPUSampler *createLinearSampler()
{
    if (!state().device) return nullptr;
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.max_anisotropy = 8.0f;
    info.enable_anisotropy = true;
    return SDL_CreateGPUSampler(state().device, &info);
}

SDL_GPUSampler *createNearestSampler()
{
    if (!state().device) return nullptr;
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_NEAREST;
    info.mag_filter = SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    return SDL_CreateGPUSampler(state().device, &info);
}

bool transformColor(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *source,
    SDL_GPUTexture *destination,
    std::uint32_t width,
    std::uint32_t height,
    float exposure_ev,
    std::uint32_t tone_mapping,
    bool encode_srgb)
{
    if (!command || !source || !destination || source == destination || width == 0u || height == 0u)
        return false;
    if (!ensureColorTransform()) return false;

    const ColorTransformData data{
        exposure_ev,
        tone_mapping,
        encode_srgb ? 1u : 0u,
        width,
        height,
        {0u, 0u, 0u},
    };
    SDL_PushGPUComputeUniformData(command, 0u, &data, sizeof data);

    SDL_GPUStorageTextureReadWriteBinding writable{};
    writable.texture = destination;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(command, &writable, 1u, nullptr, 0u);
    if (!pass) return false;
    SDL_BindGPUComputePipeline(pass, state().color_transform);
    const SDL_GPUTextureSamplerBinding sampled{source, state().color_sampler};
    SDL_BindGPUComputeSamplers(pass, 0u, &sampled, 1u);
    SDL_DispatchGPUCompute(
        pass,
        (static_cast<Uint32>(width) + 7u) / 8u,
        (static_cast<Uint32>(height) + 7u) / 8u,
        1u);
    SDL_EndGPUComputePass(pass);
    return true;
}

bool blitToSwapchain(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *source,
    std::uint32_t source_width,
    std::uint32_t source_height)
{
    if (!command || !source || !Window::Internal::window()) return false;
    SDL_GPUTexture *swapchain = nullptr;
    Uint32 width = 0u;
    Uint32 height = 0u;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            command,
            Window::Internal::window(),
            &swapchain,
            &width,
            &height))
    {
        std::fprintf(stderr, "[SDL_GPU]: swapchain acquisition failed: %s\n", SDL_GetError());
        return false;
    }
    if (!swapchain) return true;

    SDL_GPUBlitInfo info{};
    info.source.texture = source;
    info.source.w = source_width;
    info.source.h = source_height;
    info.destination.texture = swapchain;
    info.destination.w = width;
    info.destination.h = height;
    info.load_op = SDL_GPU_LOADOP_DONT_CARE;
    info.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(command, &info);
    return true;
}

void cancel(SDL_GPUCommandBuffer *command)
{
    if (command) SDL_CancelGPUCommandBuffer(command);
}

} // namespace Renderer::SDLGPU
