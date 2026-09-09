#ifdef __APPLE__

#include "Renderer/PathTracer/PathTracer.hpp"

#include "Animation/Animation.hpp"
#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/FontPass.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scene.hpp"
#include "Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <lwcgl/lwcgl.h>
#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

// Reuse the existing Metal scene implementation while replacing only its
// frame/presentation boundary. The legacy entry point remains private and is
// never used by IRenderer::render().
#define render legacyRender
#define shutdown legacyShutdown
#include "Renderer/PathTracer/PathTracerMetal.cpp"
#undef shutdown
#undef render

namespace Renderer {
namespace {

struct PrimaryDepthState {
    LWMGLTexture texture = nullptr;
    int width = 0;
    int height = 0;
};

std::unordered_map<PathTracer::Impl*, PrimaryDepthState> primary_depth_states;

void destroyPrimaryDepth(PathTracer::Impl *impl)
{
    if (!impl) return;
    const auto found = primary_depth_states.find(impl);
    if (found == primary_depth_states.end()) return;
    if (found->second.texture) Metal.destroyTexture(found->second.texture);
    primary_depth_states.erase(found);
}

LWMGLTexture ensurePrimaryDepth(PathTracer::Impl *impl)
{
    if (!impl || !Metal.isCreated()) return nullptr;

    PrimaryDepthState& state = primary_depth_states[impl];
    if (
        state.texture &&
        state.width == impl->trace_width &&
        state.height == impl->trace_height)
    {
        return state.texture;
    }

    if (state.texture) Metal.destroyTexture(state.texture);
    state = {};

    const LWMGLTextureDesc desc = {
        static_cast<std::uint32_t>(std::max(impl->trace_width, 1)),
        static_cast<std::uint32_t>(std::max(impl->trace_height, 1)),
        LWMGL_RGBA32_FLOAT,
        LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_WRITE,
        LWMGL_STORAGE_PRIVATE
    };
    state.texture = Metal.createTexture(&desc);
    if (!state.texture) {
        primary_depth_states.erase(impl);
        return nullptr;
    }

    state.width = impl->trace_width;
    state.height = impl->trace_height;
    return state.texture;
}

bool beginClearFrame(PathTracer::Impl *impl, Internal::FrameOutput& output)
{
    if (!impl || !Metal.isCreated()) return false;

    LWMGLCommand command = Metal.begin();
    if (!command) return false;

    const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
    if (Metal.beginRenderToDrawable(command, clear, 1) != 0) {
        Metal.destroyCommand(command);
        return false;
    }

    output.api = Internal::GraphicsApi::Metal;
    output.depth = Internal::DepthSource::None;
    output.width = impl->width;
    output.height = impl->height;
    output.command = command;
    output.depth_texture = nullptr;
    return true;
}

} // namespace

bool PathTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_ || !impl_->initialized || !Metal.isCreated()) return false;

    output.api = Internal::GraphicsApi::Metal;
    output.depth = Internal::DepthSource::None;
    output.width = impl_->width;
    output.height = impl_->height;
    output.command = nullptr;
    output.depth_texture = nullptr;

    if (!impl_->active()) return beginClearFrame(impl_, output);

    const int previous_trace_width = impl_->trace_width;
    const int previous_trace_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (
        impl_->trace_width != previous_trace_width ||
        impl_->trace_height != previous_trace_height)
    {
        impl_->destroyAccumulation();
        if (!impl_->createAccumulation()) {
            std::fprintf(
                stderr,
                "[PathTracer]: failed to recreate Metal accumulation texture: %s\n",
                lwmglGetLastError()
            );
            legacyShutdown();
            return false;
        }
    }

    const Scene::CameraState scene_camera = Scene::cameraState(world);
    const Impl::CameraState camera = impl_->cameraState(scene_camera);
    if (!camera.valid) return beginClearFrame(impl_, output);

    const Scene::LightState scene_light = Scene::lightState(world);
    const Impl::LightState light = impl_->lightState(scene_light);
    const std::uint64_t next_camera_signature = impl_->cameraSignature(camera);
    const std::uint64_t next_light_signature = impl_->lightSignature(light);
    const std::uint64_t next_world_revision = world.changeRevision();

    if (next_world_revision != impl_->world_revision) {
        Scene::collectRenderItems(world, impl_->render_items);
        const std::uint64_t next_scene_signature =
            impl_->sceneSignature(world, impl_->render_items);
        if (next_scene_signature != impl_->scene_signature) {
            if (!impl_->syncScene(world, impl_->render_items)) {
                std::fprintf(
                    stderr,
                    "[PathTracer]: failed to synchronize Metal world cache: %s\n",
                    lwmglGetLastError()
                );
                return false;
            }
            impl_->scene_signature = next_scene_signature;
            impl_->resetAccumulation();
        }
        impl_->world_revision = next_world_revision;
    }

    const bool camera_changed = next_camera_signature != impl_->camera_signature;
    impl_->camera_moving = camera_changed;
    if (camera_changed) {
        impl_->camera_signature = next_camera_signature;
        impl_->resetAccumulation();
    } else if (impl_->was_camera_moving) {
        impl_->resetAccumulation();
    }
    impl_->was_camera_moving = camera_changed;

    if (next_light_signature != impl_->light_signature) {
        impl_->light_signature = next_light_signature;
        impl_->resetAccumulation();
    }

    if (
        !impl_->node_buffer ||
        !impl_->triangle_buffer ||
        !impl_->material_buffer ||
        !impl_->accumulation)
    {
        return false;
    }

    LWMGLTexture primary_depth = ensurePrimaryDepth(impl_);
    if (!primary_depth) {
        std::fprintf(
            stderr,
            "[PathTracer]: failed to create Metal primary-depth texture: %s\n",
            lwmglGetLastError()
        );
        return false;
    }

    const int samples = std::clamp(impl_->settings.samples_per_frame, 1, 4);
    if (impl_->sample_count > 1000000000u - static_cast<std::uint32_t>(samples)) {
        impl_->resetAccumulation();
    }
    if (impl_->frame_index > 1000000000u) impl_->frame_index &= 3u;

    const Impl::TraceUniforms trace_uniforms = impl_->traceUniforms(camera, light);
    if (
        Metal.uploadBuffer(
            impl_->trace_uniform_buffer,
            0u,
            &trace_uniforms,
            sizeof trace_uniforms
        ) != 0)
    {
        return false;
    }

    Impl::PresentUniforms present_uniforms{};
    present_uniforms.exposure[0] = impl_->settings.exposure;
    present_uniforms.exposure[1] = impl_->camera_moving ? 1.0f : 0.0f;
    if (
        Metal.uploadBuffer(
            impl_->present_uniform_buffer,
            0u,
            &present_uniforms,
            sizeof present_uniforms
        ) != 0)
    {
        return false;
    }

    LWMGLCommand command = Metal.begin();
    if (!command) return false;

    bool ok = Metal.beginCompute(command) == 0;
    if (ok) ok = Metal.setComputePipeline(command, impl_->trace_pipeline) == 0;
    if (ok) ok = Metal.setBuffer(command, impl_->node_buffer, 0u, 0u) == 0;
    if (ok) ok = Metal.setBuffer(command, impl_->triangle_buffer, 0u, 1u) == 0;
    if (ok) ok = Metal.setBuffer(command, impl_->material_buffer, 0u, 2u) == 0;
    if (ok) ok = Metal.setBuffer(command, impl_->trace_uniform_buffer, 0u, 3u) == 0;
    if (ok) ok = Metal.setTexture(command, impl_->accumulation, 0u) == 0;
    for (std::size_t slot = 0u; ok && slot < impl_->texture_slots.size(); ++slot) {
        ok = Metal.setTexture(
            command,
            impl_->texture_slots[slot] ? impl_->texture_slots[slot] : impl_->white_texture,
            static_cast<std::uint32_t>(slot + 1u)
        ) == 0;
    }
    if (ok) ok = Metal.setTexture(command, primary_depth, 17u) == 0;
    if (ok) ok = Metal.setSampler(command, impl_->material_sampler, 0u) == 0;
    if (ok) ok = Metal.dispatch(
        command,
        static_cast<std::uint32_t>(impl_->trace_width),
        static_cast<std::uint32_t>(impl_->trace_height),
        1u
    ) == 0;
    if (ok) ok = Metal.endEncoding(command) == 0;

    const LWMGLClearColor clear = {0.0, 0.0, 0.0, 1.0};
    if (ok) ok = Metal.beginRenderToDrawable(command, clear, 1) == 0;
    if (ok) ok = Metal.setRenderPipeline(command, impl_->present_pipeline) == 0;
    if (ok) ok = Metal.setFragmentBuffer(command, impl_->present_uniform_buffer, 0u, 0u) == 0;
    if (ok) ok = Metal.setFragmentTexture(command, impl_->accumulation, 0u) == 0;
    if (ok) ok = Metal.setFragmentSampler(command, impl_->present_sampler, 0u) == 0;
    if (ok) ok = Metal.draw(command, 0u, 3u) == 0;

    if (!ok) {
        std::fprintf(stderr, "[PathTracer]: Metal frame failed: %s\n", lwmglGetLastError());
        Metal.destroyCommand(command);
        return false;
    }

    impl_->sample_count += static_cast<std::uint32_t>(samples);
    ++impl_->frame_index;
    impl_->phase_count = std::min<std::uint32_t>(impl_->phase_count + 1u, 4u);
    impl_->reset_pending = false;

    output.depth = Internal::DepthSource::LinearTexture;
    output.command = command;
    output.depth_texture = primary_depth;
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
        std::fprintf(stderr, "[PathTracer]: Metal present failed: %s\n", lwmglGetLastError());
    }

    Metal.destroyCommand(command);
    output.command = nullptr;
}

void PathTracer::shutdown()
{
    if (!impl_) return;
    if (Metal.isCreated()) {
        Metal.waitIdle();
        Internal::shutdownFonts(Internal::GraphicsApi::Metal);
        destroyPrimaryDepth(impl_);
    } else {
        primary_depth_states.erase(impl_);
    }
    legacyShutdown();
}

} // namespace Renderer

#endif
