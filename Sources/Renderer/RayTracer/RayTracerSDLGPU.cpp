#include "Renderer/RayTracer/RayTracer.hpp"

#include "Renderer/Internal/FontPass.hpp"
#include "Renderer/Internal/FrameSDLGPU.hpp"
#include "Renderer/Internal/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/Internal/ReconstructionSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/ReconstructionTraceShaders.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Internal/SceneResourcesSDLGPU.hpp"
#include "Window/Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
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
constexpr SDL_GPUTextureUsageFlags TraceSurfaceUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
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

} // namespace

struct RayTracer::Impl {
    using Clock = std::chrono::steady_clock;

    RayTracerSettings settings{};
    Frame::SDLGPU::Target frame;
    Scenes::SDLGPU::SceneResources scene;
    Internal::ReconstructionSDLGPU reconstruction;
    Reconstruction::BudgetController budget;
    SDL_GPUComputePipeline *pipeline = nullptr;
    SDL_GPUTexture *trace_color = nullptr;
    SDL_GPUTexture *trace_depth = nullptr;
    SDL_GPUTexture *trace_surface = nullptr;
    Clock::time_point previous_frame_time{};
    std::uint64_t previous_camera_signature = 0u;
    std::uint32_t frame_index = 0u;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    bool initialized = false;
    bool gpu_retained = false;
    bool have_frame_time = false;
    bool have_camera_signature = false;

    bool configured() const { return true; }
    bool active() const { return initialized && settings.enabled; }

    void updateResolution()
    {
        trace_width = std::max(width, 1);
        trace_height = std::max(height, 1);
    }

    void observeFrameTime()
    {
        const Clock::time_point now = Clock::now();
        if (have_frame_time) {
            const float elapsed = std::chrono::duration<float, std::milli>(
                now - previous_frame_time).count();
            budget.observe(elapsed, settings.reconstruction);
        }
        previous_frame_time = now;
        have_frame_time = true;
    }

    void destroyTargets()
    {
        SDL_GPUDevice *device = SDLGPU::device();
        if (device) {
            if (trace_surface) SDL_ReleaseGPUTexture(device, trace_surface);
            if (trace_depth) SDL_ReleaseGPUTexture(device, trace_depth);
            if (trace_color) SDL_ReleaseGPUTexture(device, trace_color);
        }
        trace_surface = nullptr;
        trace_depth = nullptr;
        trace_color = nullptr;
    }

    bool createTargets()
    {
        updateResolution();
        const auto w = static_cast<std::uint32_t>(trace_width);
        const auto h = static_cast<std::uint32_t>(trace_height);
        trace_color = SDLGPU::createTexture(
            SDLGPU::colorFormat(), TraceColorUsage, w, h, "Horse Ray Fresh Color");
        trace_depth = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT, TraceDepthUsage, w, h, "Horse Ray Fresh Depth");
        trace_surface = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            TraceSurfaceUsage,
            w,
            h,
            "Horse Ray Fresh Surface");
        if (trace_color && trace_depth && trace_surface) return true;
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
        reconstruction.reset();
        return createTargets();
    }

    bool dispatch(
        const Scenes::CameraState& camera,
        const GlobalIllumination::Field *global_illumination,
        Internal::FrameOutput& output,
        bool camera_moving,
        bool reset_history)
    {
        auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
        if (!command || !pipeline || !trace_color || !trace_depth || !trace_surface) return false;

        const std::uint32_t grid = budget.grid();
        const SDLGPU::FrameUniforms uniforms = SDLGPU::makeFrameUniforms(
            camera,
            trace_width,
            trace_height,
            width,
            height,
            scene.nodeCount(),
            scene.triangleCount(),
            scene.materialCount(),
            scene.textureCount(),
            Scenes::SceneCache::opacityCutoff(),
            frame_index,
            0u,
            1u,
            reset_history,
            camera_moving,
            grid,
            grid,
            grid,
            1u);
        SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

        SDL_GPUStorageTextureReadWriteBinding writable[3]{};
        writable[0].texture = trace_color;
        writable[1].texture = trace_depth;
        writable[2].texture = trace_surface;
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

        const bool reconstructed = reconstruction.resolve(
            command,
            trace_color,
            trace_depth,
            trace_surface,
            frame.color(),
            frame.linearDepth(),
            camera,
            settings.reconstruction,
            frame_index,
            grid,
            camera_moving,
            reset_history);
        ++frame_index;
        return reconstructed;
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

    const std::string trace_source =
        std::string(SDLGPU::Shaders::Trace) + SDLGPU::ReconstructionTraceShaders::Entries;
    std::string error;
    impl_->pipeline = SDLGPU::compileComputePipeline(
        trace_source.c_str(), "Horse Ray Tracer", "SparseRayMain");
    if (!impl_->pipeline ||
        !impl_->frame.resize(impl_->width, impl_->height) ||
        !impl_->reconstruction.resize(impl_->width, impl_->height) ||
        !impl_->scene.init(&error) ||
        !impl_->createTargets())
    {
        if (!error.empty()) std::fprintf(stderr, "[RayTracer/SDL_GPU]: %s\n", error.c_str());
        shutdown();
        return false;
    }

    impl_->budget.reset();
    impl_->frame_index = 0u;
    impl_->have_frame_time = false;
    impl_->have_camera_signature = false;
    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[RayTracer/SDL_GPU]: %s sparse native compute/BVH, output %dx%d, trace %dx%d\n",
        SDLGPU::driver(), impl_->width, impl_->height, impl_->trace_width, impl_->trace_height);
    return true;
}

bool RayTracer::activate()
{
    if (!impl_ || !impl_->initialized) return false;
    impl_->budget.reset();
    impl_->reconstruction.reset();
    impl_->frame_index = 0u;
    impl_->have_frame_time = false;
    impl_->have_camera_signature = false;
    return true;
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
    if (!impl_->frame.resize(impl_->width, impl_->height) ||
        !impl_->reconstruction.resize(impl_->width, impl_->height))
    {
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

    impl_->observeFrameTime();

    std::string error;
    const auto sync = impl_->scene.sync(world, &error);
    if (!sync.ok) {
        std::fprintf(stderr, "[RayTracer/SDL_GPU]: scene sync failed: %s\n", error.c_str());
        return false;
    }
    if (sync.scene_changed) impl_->reconstruction.reset();

    if (!impl_->frame.begin(output)) return false;
    output.scene_resources = &impl_->scene;

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    if (!impl_->active() || !camera.valid || impl_->scene.triangleCount() == 0u) {
        output.depth = Internal::DepthSource::None;
        output.depth_texture = nullptr;
        return clearColor(
            static_cast<SDL_GPUCommandBuffer *>(output.command), impl_->frame.color());
    }

    const std::uint64_t signature = Scenes::cameraSignature(camera);
    const bool camera_moving =
        impl_->have_camera_signature && signature != impl_->previous_camera_signature;
    impl_->previous_camera_signature = signature;
    impl_->have_camera_signature = true;

    if (!impl_->dispatch(
            camera,
            output.global_illumination,
            output,
            camera_moving,
            sync.scene_changed))
    {
        Frame::SDLGPU::cancel(output);
        return false;
    }
    output.api = Internal::GraphicsApi::SDLGPU;
    output.depth = Internal::DepthSource::LinearTexture;
    output.width = impl_->width;
    output.height = impl_->height;
    output.color_texture = impl_->frame.color();
    output.depth_texture = impl_->frame.linearDepth();
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
    impl_->reconstruction.shutdown();
    if (SDLGPU::device() && impl_->pipeline)
        SDL_ReleaseGPUComputePipeline(SDLGPU::device(), impl_->pipeline);
    impl_->pipeline = nullptr;
    impl_->destroyTargets();
    impl_->scene.clear();
    impl_->frame.shutdown();
    if (impl_->gpu_retained) SDLGPU::release();
    impl_->gpu_retained = false;
    impl_->initialized = false;
    impl_->budget.reset();
    impl_->frame_index = 0u;
    impl_->have_frame_time = false;
    impl_->have_camera_signature = false;
}

bool RayTracer::initialized() const { return impl_ && impl_->initialized; }
bool RayTracer::enabled() const { return impl_ && impl_->settings.enabled; }
void RayTracer::setEnabled(bool enabled) { if (impl_) impl_->settings.enabled = enabled; }
RayTracerSettings& RayTracer::settings() { return impl_->settings; }
const RayTracerSettings& RayTracer::settings() const { return impl_->settings; }

} // namespace Renderer
