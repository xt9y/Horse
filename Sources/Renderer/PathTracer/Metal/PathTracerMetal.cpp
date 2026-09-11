#ifdef __APPLE__

#include "Renderer/PathTracer/PathTracer.hpp"

#include "Renderer/FontPass.hpp"
#include "Renderer/Frame/Metal/FrameMetal.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/GlobalIlluminationMetal.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"
#include "Renderer/Systems/ProgressiveState.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"
#include "Renderer/Systems/Uniforms.hpp"
#include "Renderer/Trace/Metal/TraceScene.hpp"
#include "Renderer/Trace/Metal/TextureSet.hpp"

#include <lwcgl/lwcgl.h>
#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Renderer {
namespace {

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

} // namespace

struct PathTracer::Impl {
    PathTracerSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;

    Trace::Metal::TraceScene trace_scene;
    Systems::ProgressiveState progressive;
    Frame::Metal::Presenter presenter;

    LWMGLLibrary shader_library = nullptr;
    LWMGLFunction trace_function = nullptr;
    LWMGLFunction resolve_function = nullptr;
    LWMGLComputePipeline trace_pipeline = nullptr;
    LWMGLComputePipeline resolve_pipeline = nullptr;
    Trace::Metal::TextureSet targets;
    LWMGLSampler material_sampler = nullptr;
    LWMGLBuffer trace_uniform_buffer = nullptr;
    LWMGLBuffer resolve_uniform_buffer = nullptr;

    static constexpr std::size_t AccumulationTarget = 0u;
    static constexpr std::size_t ResolvedTarget = 1u;
    static constexpr std::size_t PrimaryDepthTarget = 2u;

    bool active() const
    {
        return initialized && settings.enabled;
    }

    bool configured() const
    {
        return settings.resolution_divisor > 0 &&
            settings.samples_per_frame > 0 &&
            settings.stationary_phase_grid > 0 &&
            settings.reset_phase_grid > 0 &&
            settings.moving_phase_grid > 0 &&
            settings.moving_depth_block > 0;
    }

    void updateTraceResolution()
    {
        const int divisor = settings.resolution_divisor;
        if (divisor <= 0) {
            trace_width = 0;
            trace_height = 0;
            return;
        }
        trace_width = std::max(width / divisor, 1);
        trace_height = std::max(height / divisor, 1);
    }

    bool createPrograms()
    {
        shader_library = Metal.createLibraryFromSource(
            PathTracerMetalShaders::source,
            std::strlen(PathTracerMetalShaders::source)
        );
        if (!shader_library) return false;

        trace_function = Metal.createFunction(shader_library, "trace_kernel");
        resolve_function = Metal.createFunction(shader_library, "resolve_pathtrace_kernel");
        if (!trace_function || !resolve_function) return false;

        trace_pipeline = Metal.createComputePipeline(trace_function);
        resolve_pipeline = Metal.createComputePipeline(resolve_function);
        return trace_pipeline && resolve_pipeline;
    }

    void destroyPrograms()
    {
        if (trace_pipeline) Metal.destroyComputePipeline(trace_pipeline);
        if (resolve_pipeline) Metal.destroyComputePipeline(resolve_pipeline);
        if (trace_function) Metal.destroyFunction(trace_function);
        if (resolve_function) Metal.destroyFunction(resolve_function);
        if (shader_library) Metal.destroyLibrary(shader_library);
        trace_pipeline = nullptr;
        resolve_pipeline = nullptr;
        trace_function = nullptr;
        resolve_function = nullptr;
        shader_library = nullptr;
    }

    bool createSamplers()
    {
        const LWMGLSamplerDesc material_desc = {
            LWMGL_FILTER_LINEAR,
            LWMGL_FILTER_LINEAR,
            LWMGL_ADDRESS_REPEAT,
            LWMGL_ADDRESS_REPEAT
        };
        material_sampler = Metal.createSampler(&material_desc);
        return material_sampler != nullptr;
    }

    void destroySamplers()
    {
        if (material_sampler) Metal.destroySampler(material_sampler);
        material_sampler = nullptr;
    }

    bool createUniformBuffers()
    {
        const LWMGLBufferDesc trace_desc = {
            sizeof(Systems::MetalTraceUniforms),
            LWMGL_STORAGE_SHARED
        };
        const LWMGLBufferDesc resolve_desc = {
            sizeof(Systems::MetalResolveUniforms),
            LWMGL_STORAGE_SHARED
        };
        Systems::MetalTraceUniforms trace{};
        Systems::MetalResolveUniforms resolve{};
        trace_uniform_buffer = Metal.createBuffer(&trace_desc, &trace);
        resolve_uniform_buffer = Metal.createBuffer(&resolve_desc, &resolve);
        return trace_uniform_buffer && resolve_uniform_buffer;
    }

    void destroyUniformBuffers()
    {
        if (trace_uniform_buffer) Metal.destroyBuffer(trace_uniform_buffer);
        if (resolve_uniform_buffer) Metal.destroyBuffer(resolve_uniform_buffer);
        trace_uniform_buffer = nullptr;
        resolve_uniform_buffer = nullptr;
    }

    bool createTraceTargets()
    {
        if (!targets.create(trace_width, trace_height, {
                {
                    LWMGL_RGBA32_FLOAT,
                    LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_READ | LWMGL_TEXTURE_WRITE
                },
                {LWMGL_RGBA16_FLOAT, LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE},
                {LWMGL_RGBA32_FLOAT, LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE},
            }))
        {
            return false;
        }
        progressive.reset();
        return true;
    }

    void destroyTraceTargets()
    {
        targets.clear();
        progressive.reset();
    }

    bool dispatchAndResolve(
        const Systems::CameraState& camera,
        const Systems::LightState& light,
        const GlobalIllumination::Field *global_illumination,
        LWMGLCommand& out_command)
    {
        out_command = nullptr;
        const LWMGLTexture accumulation = targets.texture(AccumulationTarget);
        const LWMGLTexture resolved = targets.texture(ResolvedTarget);
        const LWMGLTexture primary_depth = targets.texture(PrimaryDepthTarget);
        if (!trace_scene.accelerationStructure() || !accumulation || !resolved || !primary_depth)
            return false;

        Systems::MetalTraceUniforms trace = Systems::makeMetalTraceUniforms(
            camera,
            light,
            trace_width,
            trace_height,
            width,
            height,
            0u,
            trace_scene.triangleCount(),
            trace_scene.materialCount(),
            Systems::SceneCache::opacityCutoff(),
            progressive.frameIndex(),
            progressive.resetPending(),
            progressive.cameraMoving()
        );
        trace.path_policy = {
            static_cast<std::uint32_t>(settings.stationary_phase_grid),
            static_cast<std::uint32_t>(settings.reset_phase_grid),
            static_cast<std::uint32_t>(settings.moving_phase_grid),
            static_cast<std::uint32_t>(settings.moving_depth_block),
        };
        trace.counts[0] = settings.samples_per_frame;
        trace.counts[3] =
            (trace_scene.hasAlphaCutouts() ? 1 : 0) |
            (trace_scene.visibilityAll() ? 2 : 0);
        if (Metal.uploadBuffer(trace_uniform_buffer, 0u, &trace, sizeof trace) != 0) return false;

        Systems::MetalResolveUniforms resolve{};
        const std::uint32_t moving_grid = static_cast<std::uint32_t>(std::max(settings.moving_phase_grid, 1));
        resolve.params[0] = progressive.cameraMoving() ? 1u : 0u;
        resolve.params[1] = moving_grid;
        resolve.params[2] = progressive.frameIndex() % (moving_grid * moving_grid);
        if (Metal.uploadBuffer(resolve_uniform_buffer, 0u, &resolve, sizeof resolve) != 0) return false;

        LWMGLCommand command = Metal.begin();
        if (!command) return false;

        bool ok = Metal.beginCompute(command) == 0;
        if (ok) ok = Metal.setComputePipeline(command, trace_pipeline) == 0;
        if (ok) ok = trace_scene.bind(command, 1u);
        if (ok) ok = Metal.setBuffer(command, trace_uniform_buffer, 0u, 3u) == 0;
        if (ok) ok = Internal::bindGlobalIlluminationMetal(command, global_illumination, 4u);
        if (ok) ok = Metal.setAccelerationStructure(command, trace_scene.accelerationStructure(), 5u) == 0;
        if (ok) ok = Metal.setTexture(command, accumulation, 0u) == 0;
        if (ok) ok = Metal.setTexture(command, primary_depth, 33u) == 0;
        if (ok) ok = Metal.setSampler(command, material_sampler, 0u) == 0;
        if (ok) ok = Metal.dispatch(
            command,
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            1u
        ) == 0;
        if (ok) ok = Metal.endEncoding(command) == 0;

        if (ok) ok = Metal.beginCompute(command) == 0;
        if (ok) ok = Metal.setComputePipeline(command, resolve_pipeline) == 0;
        if (ok) ok = Metal.setBuffer(command, resolve_uniform_buffer, 0u, 0u) == 0;
        if (ok) ok = Metal.setTexture(command, accumulation, 0u) == 0;
        if (ok) ok = Metal.setTexture(command, resolved, 1u) == 0;
        if (ok) ok = Metal.dispatch(
            command,
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            1u
        ) == 0;
        if (ok) ok = Metal.endEncoding(command) == 0;

        if (!ok) {
            std::fprintf(stderr, "[PathTracer]: Metal trace/resolve failed: %s\n", lwmglGetLastError());
            Metal.destroyCommand(command);
            return false;
        }

        progressive.advance(static_cast<std::uint32_t>(settings.samples_per_frame));
        out_command = command;
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
    if (impl_->initialized) return true;
    if (!impl_->configured()) {
        std::fprintf(stderr, "[PathTracer]: configure the renderer before init\n");
        return false;
    }
    if (!Display.isCreated() || !Display.getNativeWindow()) {
        std::fprintf(stderr, "[PathTracer]: lwcgl Display must be created before Metal PathTracer\n");
        return false;
    }

    if (Metal.create(Display.getNativeWindow()) != 0) {
        std::fprintf(stderr, "[PathTracer]: Metal init failed: %s\n", lwmglGetLastError());
        return false;
    }
    if (Metal.supportsRayTracing() == 0) {
        std::fprintf(stderr, "[PathTracer]: native Metal ray tracing is not supported by this device\n");
        Metal.destroy();
        return false;
    }

    impl_->width = std::max(Display.getWidth(), 1);
    impl_->height = std::max(Display.getHeight(), 1);
    impl_->updateTraceResolution();
    std::string resource_error;
    if (
        Metal.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)) != 0 ||
        !impl_->presenter.init() ||
        !impl_->createPrograms() ||
        !impl_->createSamplers() ||
        !impl_->createUniformBuffers() ||
        !impl_->trace_scene.init(&resource_error) ||
        !impl_->createTraceTargets())
    {
        std::fprintf(
            stderr,
            "[PathTracer]: Metal resource initialization failed: %s%s%s\n",
            lwmglGetLastError(),
            resource_error.empty() ? "" : " / ",
            resource_error.c_str()
        );
        shutdown();
        return false;
    }

    impl_->initialized = true;
    LWMGLDeviceInfo info{};
    if (Metal.getDeviceInfo(&info) == 0) {
        std::fprintf(
            stderr,
            "[PathTracer]: Metal %s, output %dx%d, trace %dx%d\n",
            info.name,
            impl_->width,
            impl_->height,
            impl_->trace_width,
            impl_->trace_height
        );
    } else {
        std::fprintf(
            stderr,
            "[PathTracer]: Metal, output %dx%d, trace %dx%d\n",
            impl_->width,
            impl_->height,
            impl_->trace_width,
            impl_->trace_height
        );
    }
    return true;
}

bool PathTracer::activate()
{
    if (!impl_ || !impl_->initialized || !Metal.isCreated()) return false;
    if (!Metal.isSurfaceAttached || Metal.isSurfaceAttached() == 0) {
        if (!Metal.attachSurface || Metal.attachSurface(Display.getNativeWindow()) != 0) {
            std::fprintf(stderr, "[PathTracer]: Metal surface activation failed: %s\n", lwmglGetLastError());
            return false;
        }
    }
    if (Metal.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)) != 0)
    {
        std::fprintf(stderr, "[PathTracer]: Metal surface resize failed: %s\n", lwmglGetLastError());
        if (Metal.detachSurface) Metal.detachSurface();
        return false;
    }
    return true;
}

void PathTracer::deactivate()
{
    if (!impl_ || !impl_->initialized || !Metal.isCreated()) return;
    if (Metal.isSurfaceAttached && Metal.isSurfaceAttached() == 0) return;
    Metal.waitIdle();
    if (Metal.detachSurface) Metal.detachSurface();
}

void PathTracer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateTraceResolution();

    if (!impl_->initialized) return;
    if (Metal.isSurfaceAttached && Metal.isSurfaceAttached() != 0) {
        if (Metal.resize(
                static_cast<std::uint32_t>(impl_->width),
                static_cast<std::uint32_t>(impl_->height)) != 0)
        {
            std::fprintf(stderr, "[PathTracer]: Metal resize failed: %s\n", lwmglGetLastError());
            shutdown();
            return;
        }
    }

    if (impl_->trace_width == previous_width && impl_->trace_height == previous_height) {
        impl_->progressive.resetAccumulation();
        return;
    }

    impl_->destroyTraceTargets();
    if (!impl_->createTraceTargets()) {
        std::fprintf(stderr, "[PathTracer]: failed to resize Metal trace targets\n");
        shutdown();
    }
}

bool PathTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_->initialized || !Metal.isSurfaceAttached || Metal.isSurfaceAttached() == 0) return false;

    output.api = Internal::GraphicsApi::Metal;
    output.depth = Internal::DepthSource::None;
    output.width = impl_->width;
    output.height = impl_->height;
    output.command = nullptr;
    output.depth_texture = nullptr;

    if (!impl_->active()) {
        return Frame::Metal::beginClear(output);
    }

    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (previous_width != impl_->trace_width || previous_height != impl_->trace_height) {
        impl_->destroyTraceTargets();
        if (!impl_->createTraceTargets()) return false;
    }

    const Trace::Metal::TraceScene::SyncResult scene_sync =
        impl_->trace_scene.sync(world, impl_->width, impl_->height, "PathTracer");
    if (!scene_sync.ok) return false;
    if (scene_sync.scene_changed) impl_->progressive.sceneChanged();
    const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
    if (!camera.valid || impl_->trace_scene.triangleCount() == 0u) {
        return Frame::Metal::beginClear(output);
    }
    const Systems::LightState light = Systems::lightState(Systems::Scene::lightState(world));

    impl_->progressive.updateCamera(Systems::cameraSignature(camera));
    impl_->progressive.updateLight(Systems::lightSignature(light));
    impl_->progressive.updateGlobalIllumination(
        globalIlluminationSignature(output.global_illumination)
    );

    LWMGLCommand command = nullptr;
    if (!impl_->dispatchAndResolve(camera, light, output.global_illumination, command)) return false;

    output.command = command;
    output.depth = Internal::DepthSource::LinearTexture;
    output.color_texture = impl_->targets.texture(Impl::ResolvedTarget);
    output.depth_texture = impl_->targets.texture(Impl::PrimaryDepthTarget);
    return true;
}

bool PathTracer::compose(Internal::FrameOutput& output)
{
    return impl_->presenter.compose(output);
}

void PathTracer::present(Internal::FrameOutput& output)
{
    Frame::Metal::present(output);
}

void PathTracer::shutdown()
{
    if (!impl_) return;

    deactivate();
    if (Metal.isCreated()) Metal.waitIdle();
    Internal::shutdownFonts(Internal::GraphicsApi::Metal);
    Internal::shutdownGlobalIlluminationMetal();
    impl_->trace_scene.clear();
    impl_->destroyTraceTargets();
    impl_->destroyUniformBuffers();
    impl_->destroySamplers();
    impl_->destroyPrograms();
    impl_->presenter.shutdown();
    if (Metal.isCreated()) Metal.destroy();

    impl_->initialized = false;
    impl_->progressive.reset();
}

bool PathTracer::initialized() const
{
    return impl_ && impl_->initialized;
}

bool PathTracer::enabled() const
{
    return impl_ && impl_->settings.enabled;
}

void PathTracer::setEnabled(bool enabled)
{
    if (!impl_) return;
    impl_->settings.enabled = enabled;
    impl_->progressive.resetAccumulation();
}

PathTracerSettings& PathTracer::settings()
{
    return impl_->settings;
}

const PathTracerSettings& PathTracer::settings() const
{
    return impl_->settings;
}

} // namespace Renderer

#endif
