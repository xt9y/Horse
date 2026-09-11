#ifdef __APPLE__

#include "Renderer/RayTracer/RayTracer.hpp"

#include "Renderer/FontPass.hpp"
#include "Renderer/Frame/Metal/FrameMetal.hpp"
#include "Renderer/GlobalIlluminationMetal.hpp"
#include "Renderer/RayTracer/RayTracerMetalShaders.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"
#include "Renderer/Systems/Uniforms.hpp"
#include "Renderer/Trace/Metal/TraceScene.hpp"
#include "Renderer/Trace/Metal/TextureSet.hpp"

#include <lwcgl/lwcgl.h>
#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace Renderer {

struct RayTracer::Impl {
    RayTracerSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;

    Trace::Metal::TraceScene trace_scene;
    Frame::Metal::Presenter presenter;

    LWMGLLibrary shader_library = nullptr;
    LWMGLFunction trace_function = nullptr;
    LWMGLComputePipeline trace_pipeline = nullptr;
    Trace::Metal::TextureSet targets;
    LWMGLSampler material_sampler = nullptr;
    LWMGLBuffer trace_uniform_buffer = nullptr;

    static constexpr std::size_t OutputTarget = 0u;
    static constexpr std::size_t PrimaryDepthTarget = 1u;

    bool active() const { return initialized && settings.enabled; }

    bool configured() const
    {
        return settings.resolution_divisor > 0;
    }

    void updateResolution()
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
            RayTracerMetalShaders::source,
            std::strlen(RayTracerMetalShaders::source)
        );
        if (!shader_library) return false;
        trace_function = Metal.createFunction(shader_library, "raytrace_kernel");
        if (!trace_function) return false;
        trace_pipeline = Metal.createComputePipeline(trace_function);
        return trace_pipeline != nullptr;
    }

    void destroyPrograms()
    {
        if (trace_pipeline) Metal.destroyComputePipeline(trace_pipeline);
        if (trace_function) Metal.destroyFunction(trace_function);
        if (shader_library) Metal.destroyLibrary(shader_library);
        trace_pipeline = nullptr;
        trace_function = nullptr;
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
        Systems::MetalTraceUniforms trace{};
        trace_uniform_buffer = Metal.createBuffer(&trace_desc, &trace);
        return trace_uniform_buffer != nullptr;
    }

    void destroyUniformBuffers()
    {
        if (trace_uniform_buffer) Metal.destroyBuffer(trace_uniform_buffer);
        trace_uniform_buffer = nullptr;
    }

    bool createTargets()
    {
        return targets.create(trace_width, trace_height, {
            {LWMGL_RGBA16_FLOAT, LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE},
            {LWMGL_RGBA32_FLOAT, LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE},
        });
    }

    void destroyTargets()
    {
        targets.clear();
    }

    bool dispatch(
        const Systems::CameraState& camera,
        const Systems::LightState& light,
        const GlobalIllumination::Field *global_illumination,
        LWMGLCommand& out_command)
    {
        out_command = nullptr;
        const LWMGLTexture output_texture = targets.texture(OutputTarget);
        const LWMGLTexture primary_depth = targets.texture(PrimaryDepthTarget);
        if (!trace_scene.accelerationStructure() || !output_texture || !primary_depth)
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
            0u,
            false,
            false
        );
        trace.counts[3] =
            (trace_scene.hasAlphaCutouts() ? 1 : 0) |
            (trace_scene.visibilityAll() ? 2 : 0);
        if (Metal.uploadBuffer(trace_uniform_buffer, 0u, &trace, sizeof trace) != 0) return false;

        LWMGLCommand command = Metal.begin();
        if (!command) return false;
        bool ok = Metal.beginCompute(command) == 0;
        if (ok) ok = Metal.setComputePipeline(command, trace_pipeline) == 0;
        if (ok) ok = trace_scene.bind(command, 1u);
        if (ok) ok = Metal.setBuffer(command, trace_uniform_buffer, 0u, 3u) == 0;
        if (ok) ok = Internal::bindGlobalIlluminationMetal(command, global_illumination, 4u);
        if (ok) ok = Metal.setAccelerationStructure(command, trace_scene.accelerationStructure(), 5u) == 0;
        if (ok) ok = Metal.setTexture(command, output_texture, 0u) == 0;
        if (ok) ok = Metal.setTexture(command, primary_depth, 33u) == 0;
        if (ok) ok = Metal.setSampler(command, material_sampler, 0u) == 0;
        if (ok) ok = Metal.dispatch(
            command,
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            1u
        ) == 0;
        if (ok) ok = Metal.endEncoding(command) == 0;

        if (!ok) {
            std::fprintf(stderr, "[RayTracer]: Metal trace failed: %s\n", lwmglGetLastError());
            Metal.destroyCommand(command);
            return false;
        }
        out_command = command;
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
    if (impl_->initialized) return true;
    if (!impl_->configured()) {
        std::fprintf(stderr, "[RayTracer]: configure the renderer before init\n");
        return false;
    }
    if (!Display.isCreated() || !Display.getNativeWindow()) {
        std::fprintf(stderr, "[RayTracer]: lwcgl Display must be created before Metal RayTracer\n");
        return false;
    }
    if (Metal.create(Display.getNativeWindow()) != 0) {
        std::fprintf(stderr, "[RayTracer]: Metal init failed: %s\n", lwmglGetLastError());
        return false;
    }
    if (Metal.supportsRayTracing() == 0) {
        std::fprintf(stderr, "[RayTracer]: native Metal ray tracing is not supported by this device\n");
        Metal.destroy();
        return false;
    }

    impl_->width = std::max(Display.getWidth(), 1);
    impl_->height = std::max(Display.getHeight(), 1);
    impl_->updateResolution();
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
        !impl_->createTargets())
    {
        std::fprintf(
            stderr,
            "[RayTracer]: Metal resource initialization failed: %s%s%s\n",
            lwmglGetLastError(),
            resource_error.empty() ? "" : " / ",
            resource_error.c_str()
        );
        shutdown();
        return false;
    }

    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[RayTracer]: Metal native ray tracing, output %dx%d, trace %dx%d\n",
        impl_->width,
        impl_->height,
        impl_->trace_width,
        impl_->trace_height
    );
    return true;
}

bool RayTracer::activate()
{
    if (!impl_ || !impl_->initialized || !Metal.isCreated()) return false;
    if (!Metal.isSurfaceAttached || Metal.isSurfaceAttached() == 0) {
        if (!Metal.attachSurface || Metal.attachSurface(Display.getNativeWindow()) != 0) {
            std::fprintf(stderr, "[RayTracer]: Metal surface activation failed: %s\n", lwmglGetLastError());
            return false;
        }
    }
    if (Metal.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)) != 0)
    {
        std::fprintf(stderr, "[RayTracer]: Metal surface resize failed: %s\n", lwmglGetLastError());
        if (Metal.detachSurface) Metal.detachSurface();
        return false;
    }
    return true;
}

void RayTracer::deactivate()
{
    if (!impl_ || !impl_->initialized || !Metal.isCreated()) return;
    if (Metal.isSurfaceAttached && Metal.isSurfaceAttached() == 0) return;
    Metal.waitIdle();
    if (Metal.detachSurface) Metal.detachSurface();
}

void RayTracer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateResolution();
    if (!impl_->initialized) return;
    if (Metal.isSurfaceAttached && Metal.isSurfaceAttached() != 0) {
        if (Metal.resize(
                static_cast<std::uint32_t>(impl_->width),
                static_cast<std::uint32_t>(impl_->height)) != 0)
        {
            std::fprintf(stderr, "[RayTracer]: Metal resize failed: %s\n", lwmglGetLastError());
            shutdown();
            return;
        }
    }
    if (previous_width == impl_->trace_width && previous_height == impl_->trace_height) return;
    impl_->destroyTargets();
    if (!impl_->createTargets()) shutdown();
}

bool RayTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
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
    impl_->updateResolution();
    if (previous_width != impl_->trace_width || previous_height != impl_->trace_height) {
        impl_->destroyTargets();
        if (!impl_->createTargets()) return false;
    }

    const Trace::Metal::TraceScene::SyncResult scene_sync =
        impl_->trace_scene.sync(world, impl_->width, impl_->height, "RayTracer");
    if (!scene_sync.ok) return false;
    const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
    if (!camera.valid || impl_->trace_scene.triangleCount() == 0u) {
        return Frame::Metal::beginClear(output);
    }
    const Systems::LightState light = Systems::lightState(Systems::Scene::lightState(world));

    LWMGLCommand command = nullptr;
    if (!impl_->dispatch(camera, light, output.global_illumination, command)) return false;
    output.command = command;
    output.depth = Internal::DepthSource::LinearTexture;
    output.color_texture = impl_->targets.texture(Impl::OutputTarget);
    output.depth_texture = impl_->targets.texture(Impl::PrimaryDepthTarget);
    return true;
}

bool RayTracer::compose(Internal::FrameOutput& output)
{
    return impl_->presenter.compose(output);
}

void RayTracer::present(Internal::FrameOutput& output)
{
    Frame::Metal::present(output);
}

void RayTracer::shutdown()
{
    if (!impl_) return;
    deactivate();
    if (Metal.isCreated()) Metal.waitIdle();
    Internal::shutdownFonts(Internal::GraphicsApi::Metal);
    Internal::shutdownGlobalIlluminationMetal();
    impl_->trace_scene.clear();
    impl_->destroyTargets();
    impl_->destroyUniformBuffers();
    impl_->destroySamplers();
    impl_->destroyPrograms();
    impl_->presenter.shutdown();
    if (Metal.isCreated()) Metal.destroy();
    impl_->initialized = false;
}

bool RayTracer::initialized() const { return impl_ && impl_->initialized; }
bool RayTracer::enabled() const { return impl_ && impl_->settings.enabled; }
void RayTracer::setEnabled(bool enabled) { if (impl_) impl_->settings.enabled = enabled; }
RayTracerSettings& RayTracer::settings() { return impl_->settings; }
const RayTracerSettings& RayTracer::settings() const { return impl_->settings; }

} // namespace Renderer

#endif
