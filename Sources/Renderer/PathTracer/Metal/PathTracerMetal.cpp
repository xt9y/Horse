#ifdef __APPLE__

#include "Renderer/PathTracer/PathTracer.hpp"

#include "Models/Core/Texture.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/GlobalIlluminationMetal.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"
#include "Renderer/Systems/MetalSceneResources.hpp"
#include "Renderer/Systems/ProgressiveState.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"
#include "Renderer/Visibility/Visibility.hpp"
#include "Renderer/Systems/Uniforms.hpp"

#include <lwcgl/lwcgl.h>
#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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
    struct PackedPosition {
        float x;
        float y;
        float z;
    };

    static_assert(sizeof(PackedPosition) == 12u);

    PathTracerSettings settings{};
    bool initialized = false;
    bool has_alpha_cutouts = true;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t scene_signature = 0u;
    std::uint64_t visibility_signature = 0u;
    std::uint64_t visibility_world_revision = std::numeric_limits<std::uint64_t>::max();
    bool visibility_all = true;

    Systems::SceneCache scene;
    Systems::MetalSceneResources resources;
    Systems::ProgressiveState progressive;
    std::vector<Systems::Scene::RenderItem> render_items;
    std::vector<std::uint32_t> visibility_mask;

    LWMGLLibrary shader_library = nullptr;
    LWMGLFunction trace_function = nullptr;
    LWMGLFunction present_vertex_function = nullptr;
    LWMGLFunction present_fragment_function = nullptr;
    LWMGLComputePipeline trace_pipeline = nullptr;
    LWMGLRenderPipeline present_pipeline = nullptr;
    LWMGLTexture accumulation = nullptr;
    LWMGLTexture primary_depth = nullptr;
    LWMGLSampler material_sampler = nullptr;
    LWMGLSampler present_sampler = nullptr;
    LWMGLBuffer trace_uniform_buffer = nullptr;
    LWMGLBuffer present_uniform_buffer = nullptr;
    LWMGLBuffer acceleration_vertex_buffer = nullptr;
    LWMGLAccelerationStructure acceleration_structure = nullptr;

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
        present_vertex_function = Metal.createFunction(shader_library, "present_vertex");
        present_fragment_function = Metal.createFunction(shader_library, "present_fragment");
        if (!trace_function || !present_vertex_function || !present_fragment_function) return false;

        trace_pipeline = Metal.createComputePipeline(trace_function);
        present_pipeline = Metal.createRenderPipeline(
            present_vertex_function,
            present_fragment_function,
            LWMGL_BGRA8_UNORM
        );
        return trace_pipeline && present_pipeline;
    }

    void destroyPrograms()
    {
        if (trace_pipeline) Metal.destroyComputePipeline(trace_pipeline);
        if (present_pipeline) Metal.destroyRenderPipeline(present_pipeline);
        if (trace_function) Metal.destroyFunction(trace_function);
        if (present_vertex_function) Metal.destroyFunction(present_vertex_function);
        if (present_fragment_function) Metal.destroyFunction(present_fragment_function);
        if (shader_library) Metal.destroyLibrary(shader_library);
        trace_pipeline = nullptr;
        present_pipeline = nullptr;
        trace_function = nullptr;
        present_vertex_function = nullptr;
        present_fragment_function = nullptr;
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
        const LWMGLSamplerDesc present_desc = {
            LWMGL_FILTER_LINEAR,
            LWMGL_FILTER_LINEAR,
            LWMGL_ADDRESS_CLAMP,
            LWMGL_ADDRESS_CLAMP
        };
        material_sampler = Metal.createSampler(&material_desc);
        present_sampler = Metal.createSampler(&present_desc);
        return material_sampler && present_sampler;
    }

    void destroySamplers()
    {
        if (material_sampler) Metal.destroySampler(material_sampler);
        if (present_sampler) Metal.destroySampler(present_sampler);
        material_sampler = nullptr;
        present_sampler = nullptr;
    }

    bool createUniformBuffers()
    {
        const LWMGLBufferDesc trace_desc = {
            sizeof(Systems::MetalTraceUniforms),
            LWMGL_STORAGE_SHARED
        };
        const LWMGLBufferDesc present_desc = {
            sizeof(Systems::MetalPresentUniforms),
            LWMGL_STORAGE_SHARED
        };
        Systems::MetalTraceUniforms trace{};
        Systems::MetalPresentUniforms present{};
        trace_uniform_buffer = Metal.createBuffer(&trace_desc, &trace);
        present_uniform_buffer = Metal.createBuffer(&present_desc, &present);
        return trace_uniform_buffer && present_uniform_buffer;
    }

    void destroyUniformBuffers()
    {
        if (trace_uniform_buffer) Metal.destroyBuffer(trace_uniform_buffer);
        if (present_uniform_buffer) Metal.destroyBuffer(present_uniform_buffer);
        trace_uniform_buffer = nullptr;
        present_uniform_buffer = nullptr;
    }

    bool createTraceTargets()
    {
        if (trace_width <= 0 || trace_height <= 0) return false;

        const LWMGLTextureDesc accumulation_desc = {
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            LWMGL_RGBA32_FLOAT,
            LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_READ | LWMGL_TEXTURE_WRITE,
            LWMGL_STORAGE_PRIVATE
        };
        accumulation = Metal.createTexture(&accumulation_desc);
        if (!accumulation) return false;

        const LWMGLTextureDesc depth_desc = {
            static_cast<std::uint32_t>(trace_width),
            static_cast<std::uint32_t>(trace_height),
            LWMGL_RGBA32_FLOAT,
            LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE,
            LWMGL_STORAGE_PRIVATE
        };
        primary_depth = Metal.createTexture(&depth_desc);
        if (!primary_depth) {
            Metal.destroyTexture(accumulation);
            accumulation = nullptr;
            return false;
        }
        progressive.reset();
        return true;
    }

    void destroyTraceTargets()
    {
        if (primary_depth) Metal.destroyTexture(primary_depth);
        if (accumulation) Metal.destroyTexture(accumulation);
        primary_depth = nullptr;
        accumulation = nullptr;
        progressive.reset();
    }

    void updateAlphaCutoutState()
    {
        has_alpha_cutouts = false;
        for (const Systems::Scene::RenderItem& item : render_items) {
            const Models::MaterialData *material = item.material;
            if (!material) continue;
            if (material->opacity < 1.0f) {
                has_alpha_cutouts = true;
                return;
            }
            if (material->diffuse_texture == Models::INVALID_TEXTURE) continue;
            const Models::TextureAsset *texture = Models::texture(material->diffuse_texture);
            if (texture && texture->image.meaningful_alpha) {
                has_alpha_cutouts = true;
                return;
            }
        }
    }

    void destroyAccelerationStructure()
    {
        if (acceleration_structure) Metal.destroyAccelerationStructure(acceleration_structure);
        if (acceleration_vertex_buffer) Metal.destroyBuffer(acceleration_vertex_buffer);
        acceleration_structure = nullptr;
        acceleration_vertex_buffer = nullptr;
    }

    bool createAccelerationStructure()
    {
        destroyAccelerationStructure();
        if (scene.triangles().empty()) return true;

        std::vector<PackedPosition> positions;
        positions.reserve(scene.triangles().size() * 3u);
        for (const Systems::GpuTriangle& triangle : scene.triangles()) {
            positions.push_back({triangle.p0[0], triangle.p0[1], triangle.p0[2]});
            positions.push_back({triangle.p1[0], triangle.p1[1], triangle.p1[2]});
            positions.push_back({triangle.p2[0], triangle.p2[1], triangle.p2[2]});
        }

        if (positions.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            return false;

        const LWMGLBufferDesc vertex_desc = {
            positions.size() * sizeof(PackedPosition),
            LWMGL_STORAGE_SHARED
        };
        acceleration_vertex_buffer = Metal.createBuffer(&vertex_desc, positions.data());
        if (!acceleration_vertex_buffer) return false;

        const LWMGLTriangleGeometryDesc geometry = {
            acceleration_vertex_buffer,
            0u,
            static_cast<std::uint32_t>(sizeof(PackedPosition)),
            static_cast<std::uint32_t>(positions.size()),
            nullptr,
            0u,
            0u,
            1u
        };
        acceleration_structure = Metal.createTriangleAccelerationStructure(&geometry, 1u);
        if (!acceleration_structure) {
            Metal.destroyBuffer(acceleration_vertex_buffer);
            acceleration_vertex_buffer = nullptr;
            return false;
        }
        return true;
    }

    bool syncSceneIfNeeded(const Ecs::World& world)
    {
        const std::uint64_t revision = world.changeRevision();
        if (revision == world_revision && resources.ready() &&
            (scene.triangles().empty() || acceleration_structure))
        {
            return true;
        }

        Systems::Scene::collectRenderItems(world, render_items);
        updateAlphaCutoutState();
        const std::uint64_t signature = scene.signature(world, render_items);
        if (signature != scene_signature || !resources.ready() ||
            (!scene.triangles().empty() && !acceleration_structure))
        {
            std::string error;
            if (!scene.sync(
                    world,
                    render_items,
                    Systems::MetalSceneResources::MaximumTextureSlots,
                    &error))
            {
                std::fprintf(stderr, "[PathTracer]: scene cache failed: %s\n", error.c_str());
                return false;
            }
            if (!resources.sync(scene, &error)) {
                std::fprintf(stderr, "[PathTracer]: Metal scene upload failed: %s\n", error.c_str());
                return false;
            }
            if (!createAccelerationStructure()) {
                std::fprintf(
                    stderr,
                    "[PathTracer]: Metal acceleration structure failed: %s\n",
                    lwmglGetLastError()
                );
                return false;
            }
            scene_signature = signature;
            progressive.sceneChanged();
            visibility_world_revision = std::numeric_limits<std::uint64_t>::max();
            std::fprintf(
                stderr,
                "[PathTracer]: Metal native AS %zu triangles, %zu materials, alpha=%s\n",
                scene.triangles().size(),
                scene.materials().size(),
                has_alpha_cutouts ? "cutout" : "opaque"
            );
        }
        world_revision = revision;
        return true;
    }

    bool syncVisibilityIfNeeded(const Ecs::World& world)
    {
        const std::uint64_t revision = world.changeRevision();
        const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
        const std::uint64_t current_signature =
            Systems::cameraSignature(camera) ^
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width)) << 32u) ^
            static_cast<std::uint32_t>(height);
        if (revision == visibility_world_revision && current_signature == visibility_signature)
            return true;

        const Visibility::Result visibility = Visibility::system().buildEntityMask(
            world, width, height, visibility_mask);
        std::string error;
        if (!resources.syncVisibility(visibility_mask, &error)) {
            std::fprintf(stderr, "[PathTracer]: Metal visibility upload failed: %s\n", error.c_str());
            return false;
        }
        visibility_all = visibility.culled.empty();
        visibility_world_revision = revision;
        visibility_signature = current_signature;
        return true;
    }

    bool beginClearDrawable(LWMGLCommand& out_command)
    {
        out_command = nullptr;
        LWMGLCommand command = Metal.begin();
        if (!command) return false;
        const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
        if (Metal.beginRenderToDrawable(command, clear, 1) != 0) {
            Metal.destroyCommand(command);
            return false;
        }
        out_command = command;
        return true;
    }

    bool dispatchAndCompose(
        const Systems::CameraState& camera,
        const Systems::LightState& light,
        const GlobalIllumination::Field *global_illumination,
        LWMGLCommand& out_command)
    {
        out_command = nullptr;
        if (!resources.ready() || !acceleration_structure || !accumulation || !primary_depth)
            return false;

        Systems::MetalTraceUniforms trace = Systems::makeMetalTraceUniforms(
            camera,
            light,
            trace_width,
            trace_height,
            width,
            height,
            0u,
            scene.triangles().size(),
            scene.materials().size(),
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
        trace.counts[3] = (has_alpha_cutouts ? 1 : 0) | (visibility_all ? 2 : 0);
        if (Metal.uploadBuffer(trace_uniform_buffer, 0u, &trace, sizeof trace) != 0) return false;

        Systems::MetalPresentUniforms present{};
        present.exposure[0] = settings.exposure;
        present.exposure[1] = progressive.cameraMoving() ? 1.0f : 0.0f;
        const std::uint32_t moving_grid = static_cast<std::uint32_t>(std::max(settings.moving_phase_grid, 1));
        present.exposure[2] = static_cast<float>(moving_grid);
        present.exposure[3] = static_cast<float>(progressive.frameIndex() % (moving_grid * moving_grid));
        if (Metal.uploadBuffer(present_uniform_buffer, 0u, &present, sizeof present) != 0) return false;

        LWMGLCommand command = Metal.begin();
        if (!command) return false;

        bool ok = Metal.beginCompute(command) == 0;
        if (ok) ok = Metal.setComputePipeline(command, trace_pipeline) == 0;
        if (ok) ok = resources.bind(command, 1u);
        if (ok) ok = Metal.setBuffer(command, trace_uniform_buffer, 0u, 3u) == 0;
        if (ok) ok = Internal::bindGlobalIlluminationMetal(command, global_illumination, 4u);
        if (ok) ok = Metal.setAccelerationStructure(command, acceleration_structure, 5u) == 0;
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

        const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
        if (ok) ok = Metal.beginRenderToDrawable(command, clear, 1) == 0;
        if (ok) ok = Metal.setRenderPipeline(command, present_pipeline) == 0;
        if (ok) ok = Metal.setFragmentBuffer(command, present_uniform_buffer, 0u, 0u) == 0;
        if (ok) ok = Metal.setFragmentTexture(command, accumulation, 0u) == 0;
        if (ok) ok = Metal.setFragmentSampler(command, present_sampler, 0u) == 0;
        if (ok) ok = Metal.draw(command, 0u, 3u) == 0;

        if (!ok) {
            std::fprintf(stderr, "[PathTracer]: Metal frame composition failed: %s\n", lwmglGetLastError());
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
        !impl_->createPrograms() ||
        !impl_->createSamplers() ||
        !impl_->createUniformBuffers() ||
        !impl_->resources.init(&resource_error) ||
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
        LWMGLCommand command = nullptr;
        if (!impl_->beginClearDrawable(command)) return false;
        output.command = command;
        return true;
    }

    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (previous_width != impl_->trace_width || previous_height != impl_->trace_height) {
        impl_->destroyTraceTargets();
        if (!impl_->createTraceTargets()) return false;
    }

    if (!impl_->syncSceneIfNeeded(world) || !impl_->syncVisibilityIfNeeded(world)) return false;
    const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
    if (!camera.valid || impl_->scene.triangles().empty()) {
        LWMGLCommand command = nullptr;
        if (!impl_->beginClearDrawable(command)) return false;
        output.command = command;
        return true;
    }
    const Systems::LightState light = Systems::lightState(Systems::Scene::lightState(world));

    impl_->progressive.updateCamera(Systems::cameraSignature(camera));
    impl_->progressive.updateLight(Systems::lightSignature(light));
    impl_->progressive.updateGlobalIllumination(
        globalIlluminationSignature(output.global_illumination)
    );

    LWMGLCommand command = nullptr;
    if (!impl_->dispatchAndCompose(camera, light, output.global_illumination, command)) return false;

    output.command = command;
    output.depth = Internal::DepthSource::LinearTexture;
    output.depth_texture = impl_->primary_depth;
    return true;
}

void PathTracer::present(Internal::FrameOutput& output)
{
    LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    if (!command) return;

    bool ok = Metal.present(command) == 0;
    if (ok) ok = Metal.commit(command) == 0;
    if (ok) ok = Metal.wait(command) == 0;
    if (!ok) {
        std::fprintf(stderr, "[PathTracer]: Metal presentation failed: %s\n", lwmglGetLastError());
    }

    Metal.destroyCommand(command);
    output.command = nullptr;
}

void PathTracer::shutdown()
{
    if (!impl_) return;

    deactivate();
    if (Metal.isCreated()) Metal.waitIdle();
    Internal::shutdownFonts(Internal::GraphicsApi::Metal);
    Internal::shutdownGlobalIlluminationMetal();
    impl_->destroyAccelerationStructure();
    impl_->resources.clear();
    impl_->scene.clear();
    impl_->destroyTraceTargets();
    impl_->destroyUniformBuffers();
    impl_->destroySamplers();
    impl_->destroyPrograms();
    if (Metal.isCreated()) Metal.destroy();

    impl_->render_items.clear();
    impl_->visibility_mask.clear();
    impl_->has_alpha_cutouts = true;
    impl_->world_revision = std::numeric_limits<std::uint64_t>::max();
    impl_->scene_signature = 0u;
    impl_->visibility_signature = 0u;
    impl_->visibility_world_revision = std::numeric_limits<std::uint64_t>::max();
    impl_->visibility_all = true;
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
