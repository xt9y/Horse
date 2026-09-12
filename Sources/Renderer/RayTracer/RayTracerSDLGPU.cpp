#include "Renderer/RayTracer/RayTracer.hpp"

#include "Renderer/Fonts/FontPass.hpp"
#include "Renderer/Frame/FrameSDLGPU.hpp"
#include "Renderer/GlobalIllumination/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneResourcesSDLGPU.hpp"
#include "Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Renderer {
namespace {

constexpr SDL_GPUTextureUsageFlags TraceColorUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
constexpr SDL_GPUTextureUsageFlags TraceDepthUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
constexpr SDL_GPUTextureUsageFlags TraceAccumulationUsage =
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;

bool clearColor(SDL_GPUCommandBuffer *command, SDL_GPUTexture *color)
{
    if (!command || !color) return false;
    SDL_GPUColorTargetInfo target{};
    target.texture = color;
    target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &target, 1u, nullptr);
    if (!pass) return false;
    SDL_EndGPURenderPass(pass);
    return true;
}

void blitTrace(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *source,
    std::uint32_t source_width,
    std::uint32_t source_height,
    SDL_GPUTexture *destination,
    std::uint32_t destination_width,
    std::uint32_t destination_height)
{
    SDL_GPUBlitInfo info{};
    info.source.texture = source;
    info.source.w = source_width;
    info.source.h = source_height;
    info.destination.texture = destination;
    info.destination.w = destination_width;
    info.destination.h = destination_height;
    info.load_op = SDL_GPU_LOADOP_DONT_CARE;
    info.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(command, &info);
}

} // namespace

struct RayTracer::Impl {
    RayTracerSettings settings{};
    Frame::SDLGPU::Target frame;
    Scenes::SDLGPU::SceneResources scene;
    SDL_GPUComputePipeline *pipeline = nullptr;
    SDL_GPUTexture *trace_color = nullptr;
    SDL_GPUTexture *trace_depth = nullptr;
    SDL_GPUTexture *accumulation = nullptr;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    bool initialized = false;
    bool gpu_retained = false;

    bool configured() const { return settings.resolution_divisor > 0; }
    bool active() const { return initialized && settings.enabled; }

    void updateResolution()
    {
        if (settings.resolution_divisor <= 0) {
            trace_width = 0;
            trace_height = 0;
            return;
        }
        trace_width = std::max(width / settings.resolution_divisor, 1);
        trace_height = std::max(height / settings.resolution_divisor, 1);
    }

    void destroyTargets()
    {
        SDL_GPUDevice *device = SDLGPU::device();
        if (device) {
            if (accumulation) SDL_ReleaseGPUTexture(device, accumulation);
            if (trace_depth) SDL_ReleaseGPUTexture(device, trace_depth);
            if (trace_color) SDL_ReleaseGPUTexture(device, trace_color);
        }
        accumulation = nullptr;
        trace_depth = nullptr;
        trace_color = nullptr;
    }

    bool createTargets()
    {
        if (trace_width <= 0 || trace_height <= 0) return false;
        const auto w = static_cast<std::uint32_t>(trace_width);
        const auto h = static_cast<std::uint32_t>(trace_height);
        trace_color = SDLGPU::createTexture(
            SDLGPU::colorFormat(), TraceColorUsage, w, h, "Horse Ray Color");
        trace_depth = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT, TraceDepthUsage, w, h, "Horse Ray Depth");
        accumulation = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            TraceAccumulationUsage,
            w, h,
            "Horse Ray Scratch");
        if (trace_color && trace_depth && accumulation) return true;
        std::fprintf(stderr, "[RayTracer/SDL_GPU]: target creation failed: %s\n", SDL_GetError());
        destroyTargets();
        return false;
    }

    bool recreateTargetsIfNeeded()
    {
        const int old_width = trace_width;
        const int old_height = trace_height;
        updateResolution();
        if (trace_color && old_width == trace_width && old_height == trace_height) return true;
        destroyTargets();
        return createTargets();
    }

    bool dispatch(
        const Scenes::CameraState& camera,
        const GlobalIllumination::Field *global_illumination,
        Internal::FrameOutput& output)
    {
        auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
        if (!command || !pipeline || !trace_color || !trace_depth || !accumulation) return false;

        const SDLGPU::FrameUniforms uniforms = SDLGPU::makeFrameUniforms(
            camera,
            trace_width, trace_height,
            width, height,
            scene.nodeCount(), scene.triangleCount(), scene.materialCount(), scene.textureCount(),
            Scenes::SceneCache::opacityCutoff(),
            0u, 0u, 1u, true, false);
        SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

        SDL_GPUStorageTextureReadWriteBinding writable[3]{};
        writable[0].texture = trace_color;
        writable[1].texture = trace_depth;
        writable[2].texture = accumulation;
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(command, writable, 3u, nullptr, 0u);
        if (!pass) return false;
        SDL_BindGPUComputePipeline(pass, pipeline);
        scene.bindCompute(pass);
        const bool gi_ok = Internal::bindGlobalIlluminationSDLGPU(pass, global_illumination, 4u);
        if (gi_ok) {
            SDL_DispatchGPUCompute(
                pass,
                (static_cast<Uint32>(trace_width) + 7u) / 8u,
                (static_cast<Uint32>(trace_height) + 7u) / 8u,
                1u);
        }
        SDL_EndGPUComputePass(pass);
        if (!gi_ok) return false;

        blitTrace(
            command,
            trace_color,
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            frame.color(),
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height));
        return true;
    }
};

RayTracer::RayTracer() : impl_(new Impl) {}

RayTracer::~RayTracer()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool RayTracer::init()
{
    if (!impl_) return false;
    if (impl_->initialized) return true;
    if (!impl_->configured()) {
        std::fprintf(stderr, "[RayTracer]: configure the renderer before init\n");
        return false;
    }
    if (!SDLGPU::retain()) return false;
    impl_->gpu_retained = true;

    impl_->width = std::max(Window::width(), 1);
    impl_->height = std::max(Window::height(), 1);
    impl_->updateResolution();
    std::string error;
    impl_->pipeline = SDLGPU::compileComputePipeline(
        SDLGPU::Shaders::Trace, "Horse Ray Tracer", "RayMain");
    if (!impl_->pipeline ||
        !impl_->frame.resize(impl_->width, impl_->height) ||
        !impl_->scene.init(&error) ||
        !impl_->createTargets())
    {
        if (!error.empty()) std::fprintf(stderr, "[RayTracer/SDL_GPU]: %s\n", error.c_str());
        shutdown();
        return false;
    }

    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[RayTracer/SDL_GPU]: %s compute/BVH, output %dx%d, trace %dx%d\n",
        SDLGPU::driver(), impl_->width, impl_->height, impl_->trace_width, impl_->trace_height);
    return true;
}

bool RayTracer::activate()
{
    return impl_ && impl_->initialized;
}

void RayTracer::deactivate() {}

void RayTracer::resize(int width, int height)
{
    if (!impl_) return;
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    const int old_width = impl_->trace_width;
    const int old_height = impl_->trace_height;
    impl_->updateResolution();
    if (!impl_->initialized) return;
    if (!impl_->frame.resize(impl_->width, impl_->height)) {
        std::fprintf(stderr, "[RayTracer/SDL_GPU]: frame resize failed: %s\n", SDL_GetError());
        return;
    }
    if (old_width == impl_->trace_width && old_height == impl_->trace_height) return;
    impl_->destroyTargets();
    if (!impl_->createTargets())
        std::fprintf(stderr, "[RayTracer/SDL_GPU]: trace resize failed\n");
}

bool RayTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_ || !impl_->initialized) return false;
    if (!impl_->recreateTargetsIfNeeded()) return false;

    std::string error;
    const auto sync = impl_->scene.sync(world, &error);
    if (!sync.ok) {
        std::fprintf(stderr, "[RayTracer/SDL_GPU]: scene sync failed: %s\n", error.c_str());
        return false;
    }
    if (!impl_->frame.begin(output)) return false;

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    if (!impl_->active() || !camera.valid || impl_->scene.triangleCount() == 0u) {
        output.depth = Internal::DepthSource::None;
        output.depth_texture = nullptr;
        return clearColor(
            static_cast<SDL_GPUCommandBuffer *>(output.command), impl_->frame.color());
    }

    if (!impl_->dispatch(camera, output.global_illumination, output)) {
        Frame::SDLGPU::cancel(output);
        return false;
    }
    output.api = Internal::GraphicsApi::SDLGPU;
    output.depth = Internal::DepthSource::LinearTexture;
    output.width = impl_->width;
    output.height = impl_->height;
    output.color_texture = impl_->frame.color();
    output.depth_texture = impl_->trace_depth;
    output.velocity_texture = impl_->frame.velocity();
    return true;
}

bool RayTracer::compose(Internal::FrameOutput& output)
{
    return Frame::SDLGPU::compose(output);
}

void RayTracer::present(Internal::FrameOutput& output)
{
    Frame::SDLGPU::present(output);
}

void RayTracer::shutdown()
{
    if (!impl_) return;
    Internal::shutdownFonts(Internal::GraphicsApi::SDLGPU);
    Internal::shutdownGlobalIlluminationSDLGPU();
    if (SDLGPU::device() && impl_->pipeline)
        SDL_ReleaseGPUComputePipeline(SDLGPU::device(), impl_->pipeline);
    impl_->pipeline = nullptr;
    impl_->destroyTargets();
    impl_->scene.clear();
    impl_->frame.shutdown();
    if (impl_->gpu_retained) SDLGPU::release();
    impl_->gpu_retained = false;
    impl_->initialized = false;
}

bool RayTracer::initialized() const { return impl_ && impl_->initialized; }
bool RayTracer::enabled() const { return impl_ && impl_->settings.enabled; }
void RayTracer::setEnabled(bool enabled) { if (impl_) impl_->settings.enabled = enabled; }
RayTracerSettings& RayTracer::settings() { return impl_->settings; }
const RayTracerSettings& RayTracer::settings() const { return impl_->settings; }

} // namespace Renderer
