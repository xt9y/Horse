#include "Renderer/PathTracer/PathTracer.hpp"

#include "Renderer/Fonts/FontPass.hpp"
#include "Renderer/Frame/FrameSDLGPU.hpp"
#include "Renderer/GlobalIllumination/GlobalIllumination.hpp"
#include "Renderer/GlobalIllumination/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneResourcesSDLGPU.hpp"
#include "Renderer/Systems/ProgressiveState.hpp"
#include "Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
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
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;

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

void blitPathTrace(
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

struct PathTracer::Impl {
    PathTracerSettings settings{};
    Frame::SDLGPU::Target frame;
    Scenes::SDLGPU::SceneResources scene;
    Systems::ProgressiveState progressive;
    SDL_GPUComputePipeline *trace_pipeline = nullptr;
    SDL_GPUComputePipeline *resolve_pipeline = nullptr;
    SDL_GPUSampler *resolve_sampler = nullptr;
    SDL_GPUTexture *trace_color = nullptr;
    SDL_GPUTexture *trace_depth = nullptr;
    SDL_GPUTexture *accumulation = nullptr;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    bool initialized = false;
    bool gpu_retained = false;

    bool configured() const
    {
        return settings.resolution_divisor > 0 &&
            settings.samples_per_frame > 0 &&
            settings.stationary_phase_grid > 0 &&
            settings.reset_phase_grid > 0 &&
            settings.moving_phase_grid > 0 &&
            settings.moving_depth_block > 0;
    }

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
        progressive.reset();
    }

    bool createTargets()
    {
        if (trace_width <= 0 || trace_height <= 0) return false;
        const auto w = static_cast<std::uint32_t>(trace_width);
        const auto h = static_cast<std::uint32_t>(trace_height);
        trace_color = SDLGPU::createTexture(
            SDLGPU::colorFormat(), PathColorUsage, w, h, "Horse Path Color");
        trace_depth = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT, PathDepthUsage, w, h, "Horse Path Depth");
        accumulation = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            PathAccumulationUsage,
            w, h,
            "Horse Path Accumulation");
        if (trace_color && trace_depth && accumulation) {
            progressive.reset();
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
        Internal::FrameOutput& output)
    {
        auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
        if (!command || !trace_pipeline || !resolve_pipeline || !resolve_sampler ||
            !trace_color || !trace_depth || !accumulation)
            return false;

        const SDLGPU::FrameUniforms uniforms = SDLGPU::makeFrameUniforms(
            camera,
            trace_width, trace_height,
            width, height,
            scene.nodeCount(), scene.triangleCount(), scene.materialCount(), scene.textureCount(),
            Scenes::SceneCache::opacityCutoff(),
            progressive.frameIndex(),
            progressive.sampleCount(),
            static_cast<std::uint32_t>(settings.samples_per_frame),
            progressive.resetPending(),
            progressive.cameraMoving(),
            static_cast<std::uint32_t>(settings.stationary_phase_grid),
            static_cast<std::uint32_t>(settings.reset_phase_grid),
            static_cast<std::uint32_t>(settings.moving_phase_grid),
            static_cast<std::uint32_t>(settings.moving_depth_block));
        SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

        SDL_GPUStorageTextureReadWriteBinding writable[3]{};
        writable[0].texture = trace_color;
        writable[1].texture = trace_depth;
        writable[2].texture = accumulation;
        SDL_GPUComputePass *trace_pass = SDL_BeginGPUComputePass(command, writable, 3u, nullptr, 0u);
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

        const std::uint32_t moving_grid = static_cast<std::uint32_t>(
            std::max(settings.moving_phase_grid, 1));
        const std::uint32_t moving_phase = progressive.frameIndex() %
            std::max(moving_grid * moving_grid, 1u);
        const std::uint32_t resolve_params[4] = {
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            (moving_phase << 1u) | (progressive.cameraMoving() ? 1u : 0u),
            moving_grid,
        };
        SDL_PushGPUComputeUniformData(command, 0u, resolve_params, sizeof resolve_params);

        SDL_GPUStorageTextureReadWriteBinding resolved{};
        resolved.texture = trace_color;
        SDL_GPUComputePass *resolve_pass = SDL_BeginGPUComputePass(
            command, &resolved, 1u, nullptr, 0u);
        if (!resolve_pass) return false;
        SDL_BindGPUComputePipeline(resolve_pass, resolve_pipeline);
        const SDL_GPUTextureSamplerBinding accumulation_binding{
            accumulation, resolve_sampler};
        SDL_BindGPUComputeSamplers(resolve_pass, 0u, &accumulation_binding, 1u);
        SDL_DispatchGPUCompute(
            resolve_pass,
            (static_cast<Uint32>(trace_width) + 7u) / 8u,
            (static_cast<Uint32>(trace_height) + 7u) / 8u,
            1u);
        SDL_EndGPUComputePass(resolve_pass);

        progressive.advance(static_cast<std::uint32_t>(settings.samples_per_frame));
        blitPathTrace(
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
    std::string error;
    impl_->trace_pipeline = SDLGPU::compileComputePipeline(
        SDLGPU::Shaders::Trace, "Horse Path Tracer", "PathMain");
    impl_->resolve_pipeline = SDLGPU::compileComputePipeline(
        SDLGPU::Shaders::PathResolve, "Horse Path Resolve", "Main");
    impl_->resolve_sampler = SDLGPU::createNearestSampler();
    if (!impl_->trace_pipeline || !impl_->resolve_pipeline || !impl_->resolve_sampler ||
        !impl_->frame.resize(impl_->width, impl_->height) ||
        !impl_->scene.init(&error) ||
        !impl_->createTargets())
    {
        if (!error.empty()) std::fprintf(stderr, "[PathTracer/SDL_GPU]: %s\n", error.c_str());
        shutdown();
        return false;
    }

    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[PathTracer/SDL_GPU]: %s progressive compute/BVH, output %dx%d, trace %dx%d\n",
        SDLGPU::driver(), impl_->width, impl_->height, impl_->trace_width, impl_->trace_height);
    return true;
}

bool PathTracer::activate()
{
    return impl_ && impl_->initialized;
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
    if (!impl_->frame.resize(impl_->width, impl_->height)) {
        std::fprintf(stderr, "[PathTracer/SDL_GPU]: frame resize failed: %s\n", SDL_GetError());
        return;
    }
    if (old_width == impl_->trace_width && old_height == impl_->trace_height) {
        impl_->progressive.resetAccumulation();
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

    std::string error;
    const auto sync = impl_->scene.sync(world, &error);
    if (!sync.ok) {
        std::fprintf(stderr, "[PathTracer/SDL_GPU]: scene sync failed: %s\n", error.c_str());
        return false;
    }
    if (sync.scene_changed) impl_->progressive.sceneChanged();

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    const Scenes::LightState light = Scenes::lightState(Scenes::Scene::lightState(world));
    impl_->progressive.updateCamera(Scenes::cameraSignature(camera));
    impl_->progressive.updateLight(Scenes::lightSignature(light));
    impl_->progressive.updateGlobalIllumination(
        globalIlluminationSignature(output.global_illumination));

    if (!impl_->frame.begin(output)) return false;
    if (!impl_->active() || !camera.valid || impl_->scene.triangleCount() == 0u) {
        output.depth = Internal::DepthSource::None;
        output.depth_texture = nullptr;
        return clearPathColor(
            static_cast<SDL_GPUCommandBuffer *>(output.command), impl_->frame.color());
    }

    if (!impl_->dispatchAndResolve(camera, output.global_illumination, output)) {
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
    SDL_GPUDevice *device = SDLGPU::device();
    if (device) {
        if (impl_->resolve_sampler) SDL_ReleaseGPUSampler(device, impl_->resolve_sampler);
        if (impl_->resolve_pipeline) SDL_ReleaseGPUComputePipeline(device, impl_->resolve_pipeline);
        if (impl_->trace_pipeline) SDL_ReleaseGPUComputePipeline(device, impl_->trace_pipeline);
    }
    impl_->resolve_sampler = nullptr;
    impl_->resolve_pipeline = nullptr;
    impl_->trace_pipeline = nullptr;
    impl_->destroyTargets();
    impl_->scene.clear();
    impl_->frame.shutdown();
    if (impl_->gpu_retained) SDLGPU::release();
    impl_->gpu_retained = false;
    impl_->initialized = false;
    impl_->progressive.reset();
}

bool PathTracer::initialized() const { return impl_ && impl_->initialized; }
bool PathTracer::enabled() const { return impl_ && impl_->settings.enabled; }
void PathTracer::setEnabled(bool enabled)
{
    if (!impl_) return;
    impl_->settings.enabled = enabled;
    impl_->progressive.resetAccumulation();
}
PathTracerSettings& PathTracer::settings() { return impl_->settings; }
const PathTracerSettings& PathTracer::settings() const { return impl_->settings; }

} // namespace Renderer
