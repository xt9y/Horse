#include "Renderer/Rasterizer/Rasterizer.hpp"

#include "Renderer/Fonts/FontPass.hpp"
#include "Renderer/Frame/FrameSDLGPU.hpp"
#include "Renderer/GlobalIllumination/GlobalIlluminationSDLGPU.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"
#include "Renderer/SDLGPU/Uniforms.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneResourcesSDLGPU.hpp"
#include "Renderer/ShadingState.hpp"
#include "Window.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Renderer {
namespace {

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

} // namespace

struct Rasterizer::Impl {
    RasterizerSettings settings{};
    Frame::SDLGPU::Target frame;
    Scenes::SDLGPU::SceneResources scene;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUGraphicsPipeline *sky_pipeline = nullptr;
    int width = 1;
    int height = 1;
    bool initialized = false;
    bool gpu_retained = false;

    bool createPipelines()
    {
        SDL_GPUShader *vs = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Raster, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
            "Horse Raster VS", "VSMain");
        SDL_GPUShader *ps = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Raster, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
            "Horse Raster PS", "PSMain");
        SDL_GPUShader *sky_vs = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Sky, SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
            "Horse Sky VS", "SkyVS");
        SDL_GPUShader *sky_ps = SDLGPU::compileGraphicsShader(
            SDLGPU::Shaders::Sky, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
            "Horse Sky PS", "SkyPS");
        if (vs && ps) pipeline = createRasterPipeline(vs, ps, false);
        if (sky_vs && sky_ps) sky_pipeline = createRasterPipeline(sky_vs, sky_ps, true);
        if (vs) SDL_ReleaseGPUShader(SDLGPU::device(), vs);
        if (ps) SDL_ReleaseGPUShader(SDLGPU::device(), ps);
        if (sky_vs) SDL_ReleaseGPUShader(SDLGPU::device(), sky_vs);
        if (sky_ps) SDL_ReleaseGPUShader(SDLGPU::device(), sky_ps);
        if (!pipeline || !sky_pipeline) {
            std::fprintf(stderr, "[Rasterizer/SDL_GPU]: pipeline creation failed: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }

    void destroyPipelines()
    {
        if (SDLGPU::device()) {
            if (sky_pipeline) SDL_ReleaseGPUGraphicsPipeline(SDLGPU::device(), sky_pipeline);
            if (pipeline) SDL_ReleaseGPUGraphicsPipeline(SDLGPU::device(), pipeline);
        }
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
        !impl_->createPipelines())
    {
        if (!error.empty()) std::fprintf(stderr, "[Rasterizer/SDL_GPU]: %s\n", error.c_str());
        shutdown();
        return false;
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
    if (impl_->initialized && !impl_->frame.resize(impl_->width, impl_->height))
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: resize failed: %s\n", SDL_GetError());
}

bool Rasterizer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_ || !impl_->initialized) return false;
    std::string error;
    const auto sync = impl_->scene.sync(world, &error);
    if (!sync.ok) {
        std::fprintf(stderr, "[Rasterizer/SDL_GPU]: scene sync failed: %s\n", error.c_str());
        return false;
    }
    if (!impl_->frame.begin(output)) return false;

    auto *command = static_cast<SDL_GPUCommandBuffer *>(output.command);
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
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, colors, 2u, &depth);
    if (!pass) {
        Frame::SDLGPU::cancel(output);
        return false;
    }

    const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
    const SDLGPU::FrameUniforms uniforms = SDLGPU::makeFrameUniforms(
        camera, impl_->width, impl_->height, impl_->width, impl_->height,
        impl_->scene.nodeCount(), impl_->scene.triangleCount(),
        impl_->scene.materialCount(), impl_->scene.textureCount(),
        Scenes::SceneCache::opacityCutoff());

    if (camera.valid && impl_->settings.enabled) {
        SDL_BindGPUGraphicsPipeline(pass, impl_->sky_pipeline);
        impl_->scene.bindSky(pass);
        Internal::bindGlobalIlluminationSDLGPU(pass, output.global_illumination, 0u);
        SDL_PushGPUFragmentUniformData(command, 0u, &uniforms, sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3u, 1u, 0u, 0u);

        if (impl_->scene.triangleCount() > 0u) {
            SDL_BindGPUGraphicsPipeline(pass, impl_->pipeline);
            impl_->scene.bindVertex(pass);
            impl_->scene.bindFragment(pass);
            Internal::bindGlobalIlluminationSDLGPU(pass, output.global_illumination);
            SDL_PushGPUVertexUniformData(command, 0u, &uniforms, sizeof(uniforms));
            SDL_PushGPUFragmentUniformData(command, 0u, &uniforms, sizeof(uniforms));
            SDL_DrawGPUPrimitives(
                pass,
                static_cast<Uint32>(std::min<std::size_t>(
                    impl_->scene.triangleCount() * 3u, UINT32_MAX)),
                1u, 0u, 0u);
        }
    }

    SDL_EndGPURenderPass(pass);
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
    impl_->scene.clear();
    impl_->frame.shutdown();
    impl_->destroyPipelines();
    if (impl_->gpu_retained) SDLGPU::release();
    impl_->gpu_retained = false;
    impl_->initialized = false;
}

bool Rasterizer::initialized() const { return impl_ && impl_->initialized; }
bool Rasterizer::enabled() const { return impl_ && impl_->settings.enabled; }
void Rasterizer::setEnabled(bool enabled) { if (impl_) impl_->settings.enabled = enabled; }
void Rasterizer::setViewportCulling(bool value) { if (impl_) impl_->settings.viewport_culling = value; }
void Rasterizer::setShadowResolution(int value) { if (impl_) impl_->settings.shadow_resolution = value; }
void Rasterizer::setFallbackShadowResolution(int value) { if (impl_) impl_->settings.fallback_shadow_resolution = value; }
void Rasterizer::setMinimumShadowResolution(int value) { if (impl_) impl_->settings.minimum_shadow_resolution = value; }
void Rasterizer::setShadowNearPlane(float value) { if (impl_) impl_->settings.shadow_near_plane = value; }
void Rasterizer::setShadowFarScale(float value) { if (impl_) impl_->settings.shadow_far_scale = value; }
void Rasterizer::setDirectionalShadowDistance(float value) { if (impl_) impl_->settings.directional_shadow_distance = std::max(value, 1.0f); }
void Rasterizer::setClearColor(Vec4 value) { if (impl_) impl_->settings.clear_color = value; }
bool Rasterizer::viewportCulling() const { return impl_ && impl_->settings.viewport_culling; }
int Rasterizer::shadowResolution() const { return impl_ ? impl_->settings.shadow_resolution : 0; }
int Rasterizer::fallbackShadowResolution() const { return impl_ ? impl_->settings.fallback_shadow_resolution : 0; }
int Rasterizer::minimumShadowResolution() const { return impl_ ? impl_->settings.minimum_shadow_resolution : 0; }
float Rasterizer::shadowNearPlane() const { return impl_ ? impl_->settings.shadow_near_plane : 0.0f; }
float Rasterizer::shadowFarScale() const { return impl_ ? impl_->settings.shadow_far_scale : 0.0f; }
float Rasterizer::directionalShadowDistance() const { return impl_ ? impl_->settings.directional_shadow_distance : 0.0f; }
Vec4 Rasterizer::clearColor() const { return impl_ ? impl_->settings.clear_color : Vec4{}; }
RasterizerSettings& Rasterizer::settings() { return impl_->settings; }
const RasterizerSettings& Rasterizer::settings() const { return impl_->settings; }

} // namespace Renderer
