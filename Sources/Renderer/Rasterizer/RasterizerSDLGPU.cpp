#include "Renderer/Rasterizer/Rasterizer.hpp"

#include "Models/Models.hpp"
#include "Renderer/AmbientOcclusion/AmbientOcclusion.hpp"
#include "Renderer/Features.hpp"
#include "Renderer/Internal/AmbientOcclusionSDLGPU.hpp"
#include "Renderer/Internal/FontPass.hpp"
#include "Renderer/Internal/ForwardPlusSDLGPU.hpp"
#include "Renderer/Internal/FrameSDLGPU.hpp"
#include "Renderer/Internal/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/Internal/HiZSDLGPU.hpp"
#include "Renderer/Internal/OcclusionCullingSDLGPU.hpp"
#include "Renderer/Internal/RasterDrawSubmissionSDLGPU.hpp"
#include "Renderer/Internal/RasterGeometrySDLGPU.hpp"
#include "Renderer/Internal/ShadowMapsSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Internal/SceneResourcesSDLGPU.hpp"
#include "Renderer/Internal/ShadingState.hpp"
#include "Renderer/Visibility/Visibility.hpp"
#include "Window/Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer {
namespace {

constexpr std::size_t RasterMaterialTextureSlots =
    Scenes::SDLGPU::SceneResources::MaximumRasterTextureSlots;

struct alignas(16) RasterDrawUniforms {
    std::uint32_t first_vertex = 0u;
    std::uint32_t reserved0 = 0u;
    std::uint32_t reserved1 = 0u;
    std::uint32_t reserved2 = 0u;
};

static_assert(sizeof(RasterDrawUniforms) == 16u);

SDL_GPUGraphicsPipeline *createRasterPipeline(
    SDL_GPUShader *vertex,
    SDL_GPUShader *fragment,
    bool sky)
{
    SDL_GPUColorTargetDescription colors[2]{};
    colors[0].format = SDLGPU::colorFormat();
    colors[1].format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
    if (!sky) {
        colors[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colors[0].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colors[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colors[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        colors[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colors[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        colors[0].blend_state.enable_blend = true;
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = sky
        ? SDL_GPU_COMPAREOP_ALWAYS
        : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.depth_stencil_state.enable_depth_test = !sky;
    info.depth_stencil_state.enable_depth_write = !sky;
    info.target_info.color_target_descriptions = colors;
    info.target_info.num_color_targets = 2u;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(SDLGPU::device(), &info);
}

SDL_GPUGraphicsPipeline *createDepthPipeline(
    SDL_GPUShader *vertex,
    SDL_GPUShader *fragment)
{
    SDL_GPUColorTargetDescription normal{};
    normal.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.target_info.color_target_descriptions = &normal;
    info.target_info.num_color_targets = 1u;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(SDLGPU::device(), &info);
}

} // namespace

struct Rasterizer::Impl {
    RasterizerSettings settings{};
    Frame::SDLGPU::Target frame;
    Scenes::SDLGPU::SceneResources scene;
    RasterizerSDLGPU::RasterGeometry geometry;
    RasterizerSDLGPU::DrawSubmission submission;
    RasterizerSDLGPU::ShadowMaps shadows;
    AmbientOcclusion::SDLGPU::Pass ambient_occlusion;
    Visibility::SDLGPU::HiZPyramid hi_z;
    Visibility::SDLGPU::OcclusionCulling occlusion;
    Lighting::SDLGPU::ForwardPlus forward_plus;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUGraphicsPipeline *sky_pipeline = nullptr;
    SDL_GPUGraphicsPipeline *depth_pipeline = nullptr;
    SDL_GPUBuffer *visibility_buffer = nullptr;
    std::size_t visibility_capacity = 0u;
    std::vector<std::uint32_t> visibility;
    int width = 1;
    int height = 1;
    bool initialized = false;
    bool gpu_retained = false;
    bool ambient_occlusion_available = false;

    bool syncVisibility(const Ecs::World& world)
    {
        if (settings.viewport_culling) {
            Visibility::system().buildEntityMask(world, width, height, visibility);
        } else {
            std::size_t size = 1u;
            for (const Ecs::Entity entity : world.entities())
                size = std::max(size, static_cast<std::size_t>(entity) + 1u);
            visibility.assign(size, 1u);
        }

        const std::size_t bytes = visibility.size() * sizeof(std::uint32_t);
        if (!visibility_buffer || visibility_capacity < bytes) {
            SDL_GPUBuffer *replacement = SDLGPU::createBuffer(
                SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
                    SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
                bytes,
                visibility.data(),
                "Horse Raster Visibility");
            if (!replacement) return false;
            if (visibility_buffer) SDL_ReleaseGPUBuffer(SDLGPU::device(), visibility_buffer);
            visibility_buffer = replacement;
            visibility_capacity = bytes;
            return true;
        }

        SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(SDLGPU::device());
        if (!command || !SDLGPU::uploadBuffer(
                command,
                visibility_buffer,
                visibility.data(),
                bytes,
                true) ||
            !SDL_SubmitGPUCommandBuffer(command))
        {
            if (command) SDL_CancelGPUCommandBuffer(command);
            return false;
        }
        return true;
    }

    bool ensureVisibilitySystems()
    {
        if (!settings.hi_z && !settings.occlusion_culling) return true;
        if (!hi_z.resize(
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height)))
        {
            settings.hi_z = false;
            settings.occlusion_culling = false;
            return false;
        }
        if (settings.occlusion_culling && !occlusion.init()) {
            settings.occlusion_culling = false;
            if (!settings.hi_z) hi_z.clear();
            return false;
        }
        return true;
    }

    bool createPipelines()
    {
        SDL_GPUShader *vs = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Raster, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
            "Horse Raster VS", "VSMain");
        SDL_GPUShader *ps = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Raster, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
            "Horse Raster PS", "PSMain");
        SDL_GPUShader *depth_ps = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Depth, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
            "Horse Depth PS", "PSMain");
        SDL_GPUShader *sky_vs = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Sky, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
            "Horse Sky VS", "SkyVS");
        SDL_GPUShader *sky_ps = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Sky, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
            "Horse Sky PS", "SkyPS");

        if (vs && ps) pipeline = createRasterPipeline(vs, ps, false);
        if (vs && depth_ps) depth_pipeline = createDepthPipeline(vs, depth_ps);
        if (sky_vs && sky_ps) sky_pipeline = createRasterPipeline(sky_vs, sky_ps, true);

        if (vs) SDL_ReleaseGPUShader(SDLGPU::device(), vs);
        if (ps) SDL_ReleaseGPUShader(SDLGPU::device(), ps);
        if (depth_ps) SDL_ReleaseGPUShader(SDLGPU::device(), depth_ps);
        if (sky_vs) SDL_ReleaseGPUShader(SDLGPU::device(), sky_vs);
        if (sky_ps) SDL_ReleaseGPUShader(SDLGPU::device(), sky_ps);

        if (!pipeline || !sky_pipeline) {
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: pipeline creation failed: %s\n", SDL_GetError());
            return false;
        }
        if (!depth_pipeline) {
            settings.depth_prepass = false;
            settings.hi_z = false;
            settings.occlusion_culling = false;
            ambient_occlusion_available = false;
            std::fprintf(stderr,
                "[Rasterizer/SDL_GPU]: depth prepass unavailable; continuing without Hi-Z/occlusion/AO: %s\n",
                SDL_GetError());
        }
        return true;
    }

    void destroyPipelines()
    {
        if (SDLGPU::device()) {
            if (depth_pipeline) SDL_ReleaseGPUGraphicsPipeline(SDLGPU::device(), depth_pipeline);
            if (sky_pipeline) SDL_ReleaseGPUGraphicsPipeline(SDLGPU::device(), sky_pipeline);
            if (pipeline) SDL_ReleaseGPUGraphicsPipeline(SDLGPU::device(), pipeline);
        }
        depth_pipeline = nullptr;
        sky_pipeline = nullptr;
        pipeline = nullptr;
    }
};

Rasterizer::Rasterizer() : impl_(new Impl) {}

Rasterizer::~Rasterizer()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool Rasterizer::init()
{
    if (!impl_) return false;
    if (impl_->initialized) return true;
    if (!SDLGPU::retain()) return false;
    impl_->gpu_retained = true;
    impl_->width = std::max(Window::width(), 1);
    impl_->height = std::max(Window::height(), 1);

    std::string error;
    if (!impl_->frame.resize(impl_->width, impl_->height) ||
        !impl_->scene.init(&error) ||
        !impl_->shadows.init(&error) ||
        !impl_->ambient_occlusion.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)) ||
        !impl_->forward_plus.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)) ||
        !impl_->createPipelines())
    {
        if (!error.empty()) std::fprintf(stderr, "[Rasterizer/SDL_GPU]: %s\n", error.c_str());
        shutdown();
        return false;
    }

    impl_->ambient_occlusion_available =
        impl_->depth_pipeline && impl_->ambient_occlusion.ready();
    if (!impl_->ambient_occlusion_available) {
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: ambient occlusion compute unavailable; continuing without AO: %s\n",
            SDL_GetError());
    }

    if (!impl_->ensureVisibilitySystems()) {
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: Hi-Z/occlusion unavailable; continuing without GPU occlusion: %s\n",
            SDL_GetError());
    }

    if (impl_->settings.forward_plus && !impl_->forward_plus.init()) {
        impl_->settings.forward_plus = false;
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: Forward+ unavailable; continuing with full light loop: %s\n",
            SDL_GetError());
    }

    impl_->initialized = true;
    std::fprintf(stderr, "[Rasterizer/SDL_GPU]: %s PBR backend active\n", SDLGPU::driver());
    return true;
}

void Rasterizer::resize(int width, int height)
{
    if (!impl_) return;
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    if (!impl_->initialized) return;

    if (!impl_->frame.resize(impl_->width, impl_->height))
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: resize failed: %s\n", SDL_GetError());

    if (!impl_->ambient_occlusion.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)))
    {
        impl_->ambient_occlusion_available = false;
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: ambient occlusion resize failed; disabling AO runtime: %s\n",
            SDL_GetError());
    } else {
        impl_->ambient_occlusion_available =
            impl_->depth_pipeline && impl_->ambient_occlusion.ready();
    }

    if (!impl_->forward_plus.resize(
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)))
    {
        impl_->settings.forward_plus = false;
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: Forward+ resize failed; disabling Forward+: %s\n",
            SDL_GetError());
    }

    if (!impl_->ensureVisibilitySystems()) {
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: Hi-Z resize failed; disabling GPU occlusion: %s\n",
            SDL_GetError());
    }
}

bool Rasterizer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_ || !impl_->initialized) return false;
    std::string error;
    const auto sync = impl_->scene.syncRaster(world, RasterMaterialTextureSlots, &error);
    if (!sync.ok) {
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: scene sync failed: %s\n", error.c_str());
        return false;
    }
    if (!impl_->geometry.sync(world, impl_->scene.scene(), &error)) {
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: geometry sync failed: %s\n", error.c_str());
        return false;
    }
    if (!impl_->submission.sync(impl_->geometry, &error)) {
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: draw submission sync failed: %s\n", error.c_str());
        return false;
    }
    if (!impl_->syncVisibility(world)) {
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: visibility upload failed: %s\n", SDL_GetError());
        return false;
    }

    if (impl_->settings.occlusion_culling) {
        if (!impl_->ensureVisibilitySystems() ||
            !impl_->occlusion.sync(world, impl_->scene.scene(), &error))
        {
            impl_->settings.occlusion_culling = false;
            std::fprintf(stderr,
                "[Rasterizer/SDL_GPU]: occlusion setup failed; disabling occlusion: %s%s%s\n",
                error.empty() ? "" : error.c_str(),
                error.empty() ? "" : " | ",
                SDL_GetError());
        }
    }

    if (!impl_->frame.begin(output)) return false;
    output.scene_resources = nullptr;

    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    const SDLGPU::FrameUniforms uniforms = SDLGPU::makeFrameUniforms(
        camera, impl_->width, impl_->height, impl_->width, impl_->height,
        0u, impl_->geometry.vertexCount() / 3u,
        impl_->scene.materialCount(), impl_->scene.textureCount(),
        Scenes::SceneCache::opacityCutoff());
    const RasterDrawUniforms draw_uniforms{};

    if (!impl_->shadows.update(
            command,
            impl_->geometry,
            camera,
            Internal::shadingState().lighting,
            impl_->width,
            impl_->height,
            RasterizerSDLGPU::ShadowMapSettings{
                impl_->settings.shadow_resolution,
                impl_->settings.shadow_cascades,
                impl_->settings.shadow_distance,
                impl_->settings.shadow_near_plane,
            },
            &error))
    {
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: shadow update failed: %s\n", error.c_str());
        Frame::SDLGPU::cancel(output);
        return false;
    }

    const AmbientOcclusion::Settings& ao_settings =
        AmbientOcclusion::currentSettings();
    const bool ao_requested =
        Features::currentSettings().ambient_occlusion &&
        impl_->ambient_occlusion_available &&
        ao_settings.strength > 0.0f &&
        ao_settings.radius > 0.0f;
    const bool needs_depth_prepass =
        impl_->settings.depth_prepass || impl_->settings.occlusion_culling || ao_requested;
    const bool needs_hi_z = impl_->settings.hi_z || impl_->settings.occlusion_culling;

    bool depth_prepass_done = false;
    bool hi_z_ready = false;
    if (camera.valid && impl_->settings.enabled && needs_depth_prepass &&
        impl_->depth_pipeline && impl_->geometry.worldVertexCount() > 0u)
    {
        SDL_GPUColorTargetInfo normal_target{};
        normal_target.texture = impl_->frame.normal();
        normal_target.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        normal_target.load_op = SDL_GPU_LOADOP_CLEAR;
        normal_target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depth_prepass{};
        depth_prepass.texture = impl_->frame.depth();
        depth_prepass.clear_depth = 1.0f;
        depth_prepass.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_prepass.store_op = SDL_GPU_STOREOP_STORE;
        depth_prepass.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_prepass.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass *depth_pass = SDL_BeginGPURenderPass(
            command,
            &normal_target,
            1u,
            &depth_prepass
        );
        if (!depth_pass) {
            Frame::SDLGPU::cancel(output);
            return false;
        }

        SDL_BindGPUGraphicsPipeline(depth_pass, impl_->depth_pipeline);
        impl_->geometry.bind(depth_pass);
        impl_->submission.bindIndex(depth_pass);
        SDL_GPUBuffer *visibility_buffers[] = {impl_->visibility_buffer};
        SDL_BindGPUVertexStorageBuffers(depth_pass, 4u, visibility_buffers, 1u);
        SDL_PushGPUVertexUniformData(command, 0u, &uniforms, sizeof(uniforms));
        SDL_PushGPUVertexUniformData(command, 1u, &draw_uniforms, sizeof(draw_uniforms));

        for (const RasterizerSDLGPU::RasterGeometry::DrawRange& draw : impl_->geometry.draws()) {
            if (draw.camera_layer || draw.vertex_count == 0u) continue;
            const Models::MaterialData *material = Models::material(draw.material);
            if (!material || material->alpha_mode != Models::AlphaMode::Opaque) continue;

            SDL_DrawGPUIndexedPrimitives(
                depth_pass,
                static_cast<Uint32>(std::min<std::size_t>(draw.vertex_count, UINT32_MAX)),
                1u,
                static_cast<Uint32>(std::min<std::size_t>(draw.first_vertex, UINT32_MAX)),
                0,
                0u
            );
        }
        SDL_EndGPURenderPass(depth_pass);
        depth_prepass_done = true;

        if (needs_hi_z) {
            hi_z_ready = impl_->hi_z.build(command, impl_->frame.depth());
            if (!hi_z_ready) {
                impl_->settings.hi_z = false;
                impl_->settings.occlusion_culling = false;
                std::fprintf(stderr,
                    "[Rasterizer/SDL_GPU]: Hi-Z build failed; disabling GPU occlusion: %s\n",
                    SDL_GetError());
            }
        }
    }

    if (hi_z_ready && impl_->settings.occlusion_culling &&
        !impl_->occlusion.cull(
            command,
            impl_->hi_z,
            impl_->visibility_buffer,
            uniforms,
            static_cast<std::uint32_t>(impl_->width),
            static_cast<std::uint32_t>(impl_->height)))
    {
        impl_->settings.occlusion_culling = false;
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: occlusion dispatch failed; disabling occlusion: %s\n",
            SDL_GetError());
    }

    bool ao_active = false;
    if (ao_requested && depth_prepass_done) {
        ao_active = impl_->ambient_occlusion.build(
            command,
            impl_->frame.depth(),
            impl_->frame.normal(),
            uniforms,
            ao_settings,
            &error
        );
        if (!ao_active) {
            impl_->ambient_occlusion_available = false;
            std::fprintf(stderr,
                "[Rasterizer/SDL_GPU]: ambient occlusion build failed; disabling AO runtime: %s%s%s\n",
                error.empty() ? "" : error.c_str(),
                error.empty() ? "" : " | ",
                SDL_GetError());
        }
    }

    if (camera.valid && impl_->settings.enabled && impl_->settings.forward_plus &&
        !impl_->forward_plus.build(command, output.global_illumination, uniforms, &error))
    {
        impl_->settings.forward_plus = false;
        std::fprintf(stderr,
            "[Rasterizer/SDL_GPU]: Forward+ build failed; using full light loop: %s%s%s\n",
            error.empty() ? "" : error.c_str(),
            error.empty() ? "" : " | ",
            SDL_GetError());
    }

    SDL_GPUColorTargetInfo colors[2]{};
    colors[0].texture = impl_->frame.color();
    colors[0].clear_color = {
        impl_->settings.clear_color.x, impl_->settings.clear_color.y,
        impl_->settings.clear_color.z, impl_->settings.clear_color.w};
    colors[0].load_op = SDL_GPU_LOADOP_CLEAR;
    colors[0].store_op = SDL_GPU_STOREOP_STORE;
    colors[1].texture = impl_->frame.velocity();
    colors[1].clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    colors[1].load_op = SDL_GPU_LOADOP_CLEAR;
    colors[1].store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = impl_->frame.depth();
    depth.clear_depth = 1.0f;
    depth.load_op = depth_prepass_done ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, colors, 2u, &depth);
    if (!pass) {
        Frame::SDLGPU::cancel(output);
        return false;
    }

    const auto draw_batches = [&](SDL_GPURenderPass *target, bool camera_layer) -> bool {
        for (const RasterizerSDLGPU::DrawSubmission::Batch& batch : impl_->submission.batches()) {
            if (batch.camera_layer != camera_layer || batch.command_count == 0u) continue;
            const Scenes::SDLGPU::SceneResources::RasterBinding binding =
                impl_->scene.bindRasterMaterial(target, batch.material);
            if (!binding.valid) return false;

            SDLGPU::FrameUniforms material_uniforms = uniforms;
            material_uniforms.counts[2] = static_cast<std::int32_t>(std::min<std::size_t>(
                binding.material_count, static_cast<std::size_t>(INT32_MAX)));
            material_uniforms.counts[3] = static_cast<std::int32_t>(std::min<std::size_t>(
                binding.texture_count, static_cast<std::size_t>(INT32_MAX)));
            SDL_PushGPUFragmentUniformData(
                command, 0u, &material_uniforms, sizeof(material_uniforms));
            if (!impl_->submission.drawIndirect(target, batch)) return false;
        }
        return true;
    };

    if (camera.valid && impl_->settings.enabled) {
        SDL_BindGPUGraphicsPipeline(pass, impl_->sky_pipeline);
        impl_->scene.bindSky(pass);
        if (!Internal::bindGlobalIlluminationSDLGPU(
                pass,
                output.global_illumination,
                0u))
        {
            SDL_EndGPURenderPass(pass);
            Frame::SDLGPU::cancel(output);
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: sky shading-state binding failed\n");
            return false;
        }
        SDL_PushGPUFragmentUniformData(command, 0u, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);

        if (impl_->geometry.worldVertexCount() > 0u) {
            SDL_BindGPUGraphicsPipeline(pass, impl_->pipeline);
            impl_->geometry.bind(pass);
            impl_->submission.bindIndex(pass);
            SDL_GPUBuffer *visibility_buffers[] = {impl_->visibility_buffer};
            SDL_BindGPUVertexStorageBuffers(pass, 4u, visibility_buffers, 1u);
            impl_->scene.bindRasterFragment(pass);
            if (!Internal::bindGlobalIlluminationSDLGPU(
                    pass,
                    output.global_illumination,
                    2u))
            {
                SDL_EndGPURenderPass(pass);
                Frame::SDLGPU::cancel(output);
                std::fprintf(stderr, "[Rasterizer/SDL_GPU]: PBR shading-state binding failed\n");
                return false;
            }
            impl_->shadows.bind(pass);
            if (!impl_->forward_plus.bind(
                    pass,
                    command,
                    impl_->settings.forward_plus,
                    4u))
            {
                SDL_EndGPURenderPass(pass);
                Frame::SDLGPU::cancel(output);
                std::fprintf(stderr, "[Rasterizer/SDL_GPU]: Forward+ binding failed\n");
                return false;
            }
            if (!impl_->ambient_occlusion.bind(pass, command, ao_active, 6u)) {
                SDL_EndGPURenderPass(pass);
                Frame::SDLGPU::cancel(output);
                std::fprintf(stderr, "[Rasterizer/SDL_GPU]: ambient occlusion binding failed\n");
                return false;
            }
            SDL_PushGPUVertexUniformData(command, 0u, &uniforms, sizeof(uniforms));
            SDL_PushGPUVertexUniformData(command, 1u, &draw_uniforms, sizeof(draw_uniforms));
            if (!draw_batches(pass, false)) {
                SDL_EndGPURenderPass(pass);
                Frame::SDLGPU::cancel(output);
                std::fprintf(stderr, "[Rasterizer/SDL_GPU]: raster indirect batch binding failed\n");
                return false;
            }
        }
    }

    SDL_EndGPURenderPass(pass);

    if (camera.valid && impl_->settings.enabled && impl_->geometry.hasCameraGeometry()) {
        colors[0].load_op = SDL_GPU_LOADOP_LOAD;
        colors[1].load_op = SDL_GPU_LOADOP_LOAD;
        depth.texture = impl_->frame.cameraDepth();
        if (!depth.texture) {
            Frame::SDLGPU::cancel(output);
            return false;
        }
        depth.clear_depth = 1.0f;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass *camera_pass = SDL_BeginGPURenderPass(command, colors, 2u, &depth);
        if (!camera_pass) {
            Frame::SDLGPU::cancel(output);
            return false;
        }

        SDL_BindGPUGraphicsPipeline(camera_pass, impl_->pipeline);
        impl_->geometry.bind(camera_pass);
        impl_->submission.bindIndex(camera_pass);
        SDL_GPUBuffer *visibility_buffers[] = {impl_->visibility_buffer};
        SDL_BindGPUVertexStorageBuffers(camera_pass, 4u, visibility_buffers, 1u);
        impl_->scene.bindRasterFragment(camera_pass);
        if (!Internal::bindGlobalIlluminationSDLGPU(
                camera_pass,
                output.global_illumination,
                2u))
        {
            SDL_EndGPURenderPass(camera_pass);
            Frame::SDLGPU::cancel(output);
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: camera PBR shading-state binding failed\n");
            return false;
        }
        impl_->shadows.bind(camera_pass);
        if (!impl_->forward_plus.bind(
                camera_pass,
                command,
                impl_->settings.forward_plus,
                4u))
        {
            SDL_EndGPURenderPass(camera_pass);
            Frame::SDLGPU::cancel(output);
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: Forward+ binding failed\n");
            return false;
        }
        if (!impl_->ambient_occlusion.bind(camera_pass, command, false, 6u)) {
            SDL_EndGPURenderPass(camera_pass);
            Frame::SDLGPU::cancel(output);
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: ambient occlusion binding failed\n");
            return false;
        }
        SDL_PushGPUVertexUniformData(command, 0u, &uniforms, sizeof(uniforms));
        SDL_PushGPUVertexUniformData(command, 1u, &draw_uniforms, sizeof(draw_uniforms));
        if (!draw_batches(camera_pass, true)) {
            SDL_EndGPURenderPass(camera_pass);
            Frame::SDLGPU::cancel(output);
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: camera raster indirect batch binding failed\n");
            return false;
        }
        SDL_EndGPURenderPass(camera_pass);
    }

    output.api = Internal::GraphicsApi::SDLGPU;
    output.depth = Internal::DepthSource::Native;
    output.depth_texture = impl_->frame.depth();
    return true;
}

bool Rasterizer::compose(Internal::FrameOutput& output)
{
    return Frame::SDLGPU::compose(output);
}

void Rasterizer::present(Internal::FrameOutput& output)
{
    Frame::SDLGPU::present(output);
}

void Rasterizer::shutdown()
{
    if (!impl_) return;
    Internal::shutdownFonts(Internal::GraphicsApi::SDLGPU);
    Internal::shutdownGlobalIlluminationSDLGPU();
    impl_->ambient_occlusion.clear();
    impl_->forward_plus.clear();
    impl_->occlusion.clear();
    impl_->hi_z.clear();
    impl_->shadows.clear();
    impl_->submission.clear();
    impl_->geometry.clear();
    if (impl_->visibility_buffer && SDLGPU::device())
        SDL_ReleaseGPUBuffer(SDLGPU::device(), impl_->visibility_buffer);
    impl_->visibility_buffer = nullptr;
    impl_->visibility_capacity = 0u;
    impl_->visibility.clear();
    impl_->scene.clear();
    impl_->frame.shutdown();
    impl_->destroyPipelines();
    if (impl_->gpu_retained) SDLGPU::release();
    impl_->gpu_retained = false;
    impl_->ambient_occlusion_available = false;
    impl_->initialized = false;
}

bool Rasterizer::initialized() const { return impl_ && impl_->initialized; }
bool Rasterizer::enabled() const { return impl_ && impl_->settings.enabled; }
void Rasterizer::setEnabled(bool enabled) { if (impl_) impl_->settings.enabled = enabled; }
void Rasterizer::setViewportCulling(bool value) { if (impl_) impl_->settings.viewport_culling = value; }
void Rasterizer::setDepthPrepass(bool value) { if (impl_) impl_->settings.depth_prepass = value; }
void Rasterizer::setHiZ(bool value) { if (impl_) impl_->settings.hi_z = value; }
void Rasterizer::setOcclusionCulling(bool value) { if (impl_) impl_->settings.occlusion_culling = value; }
void Rasterizer::setForwardPlus(bool value) { if (impl_) impl_->settings.forward_plus = value; }
void Rasterizer::setShadowResolution(int value) { if (impl_) impl_->settings.shadow_resolution = std::max(value, 1); }
void Rasterizer::setShadowCascades(int value) { if (impl_) impl_->settings.shadow_cascades = std::max(value, 1); }
void Rasterizer::setShadowDistance(float value) { if (impl_) impl_->settings.shadow_distance = std::max(value, 1.0f); }
void Rasterizer::setShadowNearPlane(float value) { if (impl_) impl_->settings.shadow_near_plane = std::max(value, 1.0e-4f); }
void Rasterizer::setClearColor(Vec4 value) { if (impl_) impl_->settings.clear_color = value; }
bool Rasterizer::viewportCulling() const { return impl_ && impl_->settings.viewport_culling; }
bool Rasterizer::depthPrepass() const { return impl_ && impl_->settings.depth_prepass; }
bool Rasterizer::hiZ() const { return impl_ && impl_->settings.hi_z; }
bool Rasterizer::occlusionCulling() const { return impl_ && impl_->settings.occlusion_culling; }
bool Rasterizer::forwardPlus() const { return impl_ && impl_->settings.forward_plus; }
int Rasterizer::shadowResolution() const { return impl_ ? impl_->settings.shadow_resolution : 0; }
int Rasterizer::shadowCascades() const { return impl_ ? impl_->settings.shadow_cascades : 0; }
float Rasterizer::shadowDistance() const { return impl_ ? impl_->settings.shadow_distance : 0.0f; }
float Rasterizer::shadowNearPlane() const { return impl_ ? impl_->settings.shadow_near_plane : 0.0f; }
Vec4 Rasterizer::clearColor() const { return impl_ ? impl_->settings.clear_color : Vec4{}; }
RasterizerSettings& Rasterizer::settings() { return impl_->settings; }
const RasterizerSettings& Rasterizer::settings() const { return impl_->settings; }

} // namespace Renderer