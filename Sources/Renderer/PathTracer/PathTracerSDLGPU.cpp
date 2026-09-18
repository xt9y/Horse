#include "Renderer/PathTracer/PathTracer.hpp"

#include "Renderer/Internal/FontPass.hpp"
#include "Renderer/Internal/FrameSDLGPU.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/Internal/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/Internal/ReconstructionSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/ReconstructionTraceShaders.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Internal/SceneResourcesSDLGPU.hpp"
#include "Renderer/Internal/ShadingState.hpp"
#include "Window/Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Renderer {
namespace {

constexpr SDL_GPUTextureUsageFlags PathColorUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
constexpr SDL_GPUTextureUsageFlags PathDepthUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
constexpr SDL_GPUTextureUsageFlags PathAccumulationUsage =
    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
constexpr SDL_GPUTextureUsageFlags PathSurfaceUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

class ProgressiveState {
public:
    void reset()
    {
        sample_count_ = 0u;
        frame_index_ = 0u;
        reset_pending_ = true;
        camera_moving_ = false;
        was_camera_moving_ = false;
        camera_signature_ = 0u;
        light_signature_ = 0u;
        gi_signature_ = 0u;
    }

    void resetAccumulation()
    {
        sample_count_ = 0u;
        reset_pending_ = true;
    }

    bool updateCamera(std::uint64_t signature)
    {
        const bool changed = signature != camera_signature_;
        camera_moving_ = changed;
        if (changed) {
            camera_signature_ = signature;
            resetAccumulation();
        } else if (was_camera_moving_) {
            resetAccumulation();
        }
        was_camera_moving_ = changed;
        return changed;
    }

    bool updateLight(std::uint64_t signature)
    {
        if (signature == light_signature_) return false;
        light_signature_ = signature;
        resetAccumulation();
        return true;
    }

    bool updateGlobalIllumination(std::uint64_t signature)
    {
        if (signature == gi_signature_) return false;
        gi_signature_ = signature;
        resetAccumulation();
        return true;
    }

    void sceneChanged() { resetAccumulation(); }

    void advance(std::uint32_t samples)
    {
        sample_count_ += samples;
        ++frame_index_;
        reset_pending_ = false;
    }

    std::uint32_t sampleCount() const { return sample_count_; }
    std::uint32_t frameIndex() const { return frame_index_; }
    bool resetPending() const { return reset_pending_; }
    bool cameraMoving() const { return camera_moving_; }

private:
    std::uint32_t sample_count_ = 0u;
    std::uint32_t frame_index_ = 0u;
    bool reset_pending_ = true;
    bool camera_moving_ = false;
    bool was_camera_moving_ = false;
    std::uint64_t camera_signature_ = 0u;
    std::uint64_t light_signature_ = 0u;
    std::uint64_t gi_signature_ = 0u;
};

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t globalIlluminationSignature(const GlobalIllumination::Field *field)
{
    if (!field || !field->valid()) return 0u;
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, static_cast<std::uint32_t>(field->revision));
    hashValue(hash, static_cast<std::uint32_t>(field->revision >> 32u));
    hashFloat(hash, field->intensity);
    return hash;
}

bool clearPathColor(SDL_GPUCommandBuffer *command, SDL_GPUTexture *color)
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

bool clearAccumulation(SDL_GPUCommandBuffer *command, SDL_GPUTexture *accumulation)
{
    if (!command || !accumulation) return false;
    SDL_GPUColorTargetInfo target{};
    target.texture = accumulation;
    target.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, &target, 1u, nullptr);
    if (!pass) return false;
    SDL_EndGPURenderPass(pass);
    return true;
}

} // namespace

struct PathTracer::Impl {
    using Clock = std::chrono::steady_clock;

    PathTracerSettings settings{};
    Frame::SDLGPU::Target frame;
    Scenes::SDLGPU::SceneResources scene;
    Internal::ReconstructionSDLGPU reconstruction;
    Reconstruction::BudgetController budget;
    ProgressiveState progressive;
    SDL_GPUComputePipeline *trace_pipeline = nullptr;
    SDL_GPUTexture *trace_color = nullptr;
    SDL_GPUTexture *trace_depth = nullptr;
    SDL_GPUTexture *accumulation = nullptr;
    SDL_GPUTexture *trace_surface = nullptr;
    Clock::time_point previous_frame_time{};
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    bool initialized = false;
    bool gpu_retained = false;
    bool have_frame_time = false;

    bool configured() const
    {
        return settings.samples_per_frame > 0;
    }

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
            if (accumulation) SDL_ReleaseGPUTexture(device, accumulation);
            if (trace_depth) SDL_ReleaseGPUTexture(device, trace_depth);
            if (trace_color) SDL_ReleaseGPUTexture(device, trace_color);
        }
        trace_surface = nullptr;
        accumulation = nullptr;
        trace_depth = nullptr;
        trace_color = nullptr;
        progressive.reset();
    }

    bool createTargets()
    {
        updateResolution();
        const auto w = static_cast<std::uint32_t>(trace_width);
        const auto h = static_cast<std::uint32_t>(trace_height);
        trace_color = SDLGPU::createTexture(
            SDLGPU::colorFormat(), PathColorUsage, w, h, "Horse Path Fresh Color");
        trace_depth = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT, PathDepthUsage, w, h, "Horse Path Fresh Depth");
        accumulation = SDLGPU::createTexture(
            SDLGPU::colorFormat(),
            PathAccumulationUsage,
            w,
            h,
            "Horse Path Accumulation");
        trace_surface = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            PathSurfaceUsage,
            w,
            h,
            "Horse Path Fresh Surface");
        if (trace_color && trace_depth && accumulation && trace_surface) {
            progressive.reset();
            reconstruction.reset();
            return true;
        }
        std::fprintf(stderr, "[PathTracer/SDL_GPU]: target creation failed: %s\n", SDL_GetError());
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

    bool dispatchAndResolve(
        const Scenes::CameraState& camera,
        const GlobalIllumination::Field *global_illumination,
        Internal::FrameOutput& output,
        bool reset_history)
    {
        auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
        if (!command || !trace_pipeline || !trace_color || !trace_depth ||
            !accumulation || !trace_surface)
            return false;

        if (progressive.resetPending() && !clearAccumulation(command, accumulation))
            return false;

        const std::uint32_t grid = budget.grid();
        const std::uint32_t frame_index = progressive.frameIndex();
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
            progressive.sampleCount(),
            static_cast<std::uint32_t>(settings.samples_per_frame),
            false,
            progressive.cameraMoving(),
            grid,
            grid,
            grid,
            1u);
        SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

        SDL_GPUStorageTextureReadWriteBinding writable[4]{};
        writable[0].texture = trace_color;
        writable[1].texture = trace_depth;
        writable[2].texture = accumulation;
        writable[3].texture = trace_surface;
        SDL_GPUComputePass *trace_pass = SDL_BeginGPUComputePass(command, writable, 4u, nullptr, 0u);
        if (!trace_pass) return false;
        SDL_BindGPUComputePipeline(trace_pass, trace_pipeline);
        scene.bindCompute(trace_pass);
        const bool gi_ok = Internal::bindGlobalIlluminationSDLGPU(
            trace_pass, global_illumination, 4u);
        if (gi_ok) {
            SDL_DispatchGPUCompute(
                trace_pass,
                (static_cast<Uint32>(trace_width) + 7u) / 8u,
                (static_cast<Uint32>(trace_height) + 7u) / 8u,
                1u);
        }
        SDL_EndGPUComputePass(trace_pass);
        if (!gi_ok) return false;

        if (!reconstruction.resolve(
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
                progressive.cameraMoving(),
                reset_history))
            return false;

        progressive.advance(static_cast<std::uint32_t>(settings.samples_per_frame));
        return true;
    }
};

PathTracer::PathTracer() : impl_(new Impl) {}

PathTracer::~PathTracer()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool PathTracer::init()
{
    if (!impl_) return false;
    if (impl_->initialized) return true;
    if (!impl_->configured()) {
        std::fprintf(stderr, "[PathTracer]: configure the renderer before init\n");
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
    impl_->trace_pipeline = SDLGPU::compileComputePipeline(
        trace_source.c_str(), "Horse Path Tracer", "SparsePathMain");
    if (!impl_->trace_pipeline ||
        !impl_->frame.resize(impl_->width, impl_->height) ||
        !impl_->reconstruction.resize(impl_->width, impl_->height) ||
        !impl_->scene.init(&error) ||
        !impl_->createTargets())
    {
        if (!error.empty()) std::fprintf(stderr, "[PathTracer/SDL_GPU]: %s\n", error.c_str());
        shutdown();
        return false;
    }

    impl_->budget.reset();
    impl_->progressive.reset();
    impl_->have_frame_time = false;
    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[PathTracer/SDL_GPU]: %s sparse native progressive compute/BVH, output %dx%d, trace %dx%d\n",
        SDLGPU::driver(), impl_->width, impl_->height, impl_->trace_width, impl_->trace_height);
    return true;
}

bool PathTracer::activate()
{
    if (!impl_ || !impl_->initialized) return false;
    impl_->budget.reset();
    impl_->progressive.reset();
    impl_->reconstruction.reset();
    impl_->have_frame_time = false;
    return true;
}

void PathTracer::deactivate() {}

void PathTracer::resize(int width, int height)
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
        std::fprintf(stderr, "[PathTracer/SDL_GPU]: frame resize failed: %s\n", SDL_GetError());
        return;
    }
    if (old_width == impl_->trace_width && old_height == impl_->trace_height) {
        impl_->progressive.resetAccumulation();
        impl_->reconstruction.reset();
        return;
    }
    impl_->destroyTargets();
    if (!impl_->createTargets())
        std::fprintf(stderr, "[PathTracer/SDL_GPU]: trace resize failed\n");
}

bool PathTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_ || !impl_->initialized) return false;
    if (!impl_->recreateTargetsIfNeeded()) return false;

    impl_->observeFrameTime();

    std::string error;
    const auto sync = impl_->scene.sync(world, &error);
    if (!sync.ok) {
        std::fprintf(stderr, "[PathTracer/SDL_GPU]: scene sync failed: %s\n", error.c_str());
        return false;
    }
    if (sync.scene_changed) impl_->progressive.sceneChanged();

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    const bool camera_moving = impl_->progressive.updateCamera(Scenes::cameraSignature(camera));
    const bool light_changed = impl_->progressive.updateLight(Internal::shadingState().lighting.revision);
    const bool gi_changed = impl_->progressive.updateGlobalIllumination(
        globalIlluminationSignature(output.global_illumination));
    const bool reset_history = sync.scene_changed || light_changed || gi_changed;
    if (reset_history) impl_->reconstruction.reset();

    if (!impl_->frame.begin(output)) return false;
    output.scene_resources = &impl_->scene;
    if (!impl_->active() || !camera.valid || impl_->scene.triangleCount() == 0u) {
        output.depth = Internal::DepthSource::None;
        output.depth_texture = nullptr;
        return clearPathColor(
            static_cast<SDL_GPUCommandBuffer *>(output.command), impl_->frame.color());
    }

    (void)camera_moving;
    if (!impl_->dispatchAndResolve(camera, output.global_illumination, output, reset_history)) {
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

bool PathTracer::compose(Internal::FrameOutput& output)
{
    return Frame::SDLGPU::compose(output);
}

void PathTracer::present(Internal::FrameOutput& output)
{
    Frame::SDLGPU::present(output);
}

void PathTracer::shutdown()
{
    if (!impl_) return;
    Internal::shutdownFonts(Internal::GraphicsApi::SDLGPU);
    Internal::shutdownGlobalIlluminationSDLGPU();
    impl_->reconstruction.shutdown();
    SDL_GPUDevice *device = SDLGPU::device();
    if (device && impl_->trace_pipeline)
        SDL_ReleaseGPUComputePipeline(device, impl_->trace_pipeline);
    impl_->trace_pipeline = nullptr;
    impl_->destroyTargets();
    impl_->scene.clear();
    impl_->frame.shutdown();
    if (impl_->gpu_retained) SDLGPU::release();
    impl_->gpu_retained = false;
    impl_->initialized = false;
    impl_->budget.reset();
    impl_->progressive.reset();
    impl_->have_frame_time = false;
}

bool PathTracer::initialized() const { return impl_ && impl_->initialized; }
bool PathTracer::enabled() const { return impl_ && impl_->settings.enabled; }
void PathTracer::setEnabled(bool enabled)
{
    if (!impl_) return;
    impl_->settings.enabled = enabled;
    impl_->progressive.resetAccumulation();
    impl_->reconstruction.reset();
}
PathTracerSettings& PathTracer::settings() { return impl_->settings; }
const PathTracerSettings& PathTracer::settings() const { return impl_->settings; }

} // namespace Renderer
