#include "Renderer/Internal/ReconstructionSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/ReconstructionShaders.hpp"
#include "Renderer/Trace/CameraProjection.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace Renderer::Internal {
namespace {

constexpr SDL_GPUTextureUsageFlags HistoryUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

struct alignas(16) ReconstructionUniforms {
    std::array<float, 4> current_position_near{};
    std::array<float, 4> current_forward_far{};
    std::array<float, 4> current_right_aspect{};
    std::array<float, 4> current_up_tan_half_fov{};
    std::array<float, 4> current_projection_alpha{};
    std::array<float, 4> previous_position_near{};
    std::array<float, 4> previous_forward_far{};
    std::array<float, 4> previous_right_aspect{};
    std::array<float, 4> previous_up_tan_half_fov{};
    std::array<float, 4> previous_projection_alpha{};
    std::array<std::uint32_t, 4> resolution_grid_phase{};
    std::array<std::uint32_t, 4> control{};
    std::array<float, 4> params{};
};

static_assert(sizeof(ReconstructionUniforms) == 208u);

void encodeCamera(
    const Scenes::CameraState& camera,
    int width,
    int height,
    std::array<float, 4>& position_near,
    std::array<float, 4>& forward_far,
    std::array<float, 4>& right_aspect,
    std::array<float, 4>& up_tan_half_fov,
    std::array<float, 4>& projection_alpha)
{
    const float viewport_aspect = static_cast<float>(std::max(width, 1)) /
        static_cast<float>(std::max(height, 1));
    const Trace::CameraProjectionEncoding projection =
        Trace::cameraProjectionEncoding(camera, viewport_aspect);

    position_near = {
        camera.position.x,
        camera.position.y,
        camera.position.z,
        std::max(camera.near_plane, 1.0e-4f),
    };
    forward_far = {
        camera.forward.x,
        camera.forward.y,
        camera.forward.z,
        camera.far_plane > camera.near_plane
            ? camera.far_plane
            : std::numeric_limits<float>::max(),
    };
    right_aspect = {
        camera.right.x,
        camera.right.y,
        camera.right.z,
        std::max(projection.aspect, 1.0e-6f),
    };
    up_tan_half_fov = {
        camera.up.x,
        camera.up.y,
        camera.up.z,
        camera.projection == Camera::Projection::Perspective
            ? std::max(projection.scale, 1.0e-6f)
            : 1.0f,
    };
    projection_alpha = {
        std::max(std::abs(camera.xmag), 1.0e-6f),
        std::max(std::abs(camera.ymag), 1.0e-6f),
        camera.projection == Camera::Projection::Orthographic ? 1.0f : 0.0f,
        0.0f,
    };
}

} // namespace

ReconstructionSDLGPU::~ReconstructionSDLGPU()
{
    shutdown();
}

bool ReconstructionSDLGPU::ensurePipeline()
{
    if (pipeline_ && nearest_sampler_) return true;
    if (!SDLGPU::device()) return false;

    if (!pipeline_)
        pipeline_ = SDLGPU::compileComputePipeline(
            SDLGPU::ReconstructionShaders::Resolve,
            "Horse Native Reconstruction",
            "Main");
    if (!nearest_sampler_)
        nearest_sampler_ = SDLGPU::createNearestSampler();

    if (pipeline_ && nearest_sampler_) return true;
    std::fprintf(
        stderr,
        "[Reconstruction/SDL_GPU]: pipeline initialization failed: %s\n",
        SDL_GetError());
    return false;
}

bool ReconstructionSDLGPU::createHistory()
{
    if (!SDLGPU::device()) return false;

    const auto width = static_cast<std::uint32_t>(std::max(width_, 1));
    const auto height = static_cast<std::uint32_t>(std::max(height_, 1));
    for (std::uint32_t index = 0u; index < 2u; ++index) {
        history_color_[index] = SDLGPU::createTexture(
            SDLGPU::colorFormat(),
            HistoryUsage,
            width,
            height,
            index == 0u ? "Horse Reconstruction Color A" : "Horse Reconstruction Color B");
        history_depth_[index] = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            HistoryUsage,
            width,
            height,
            index == 0u ? "Horse Reconstruction Depth A" : "Horse Reconstruction Depth B");
        history_surface_[index] = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            HistoryUsage,
            width,
            height,
            index == 0u ? "Horse Reconstruction Surface A" : "Horse Reconstruction Surface B");
        history_age_[index] = SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            HistoryUsage,
            width,
            height,
            index == 0u ? "Horse Reconstruction Age A" : "Horse Reconstruction Age B");

        if (!history_color_[index] || !history_depth_[index] ||
            !history_surface_[index] || !history_age_[index])
        {
            std::fprintf(
                stderr,
                "[Reconstruction/SDL_GPU]: history target creation failed: %s\n",
                SDL_GetError());
            destroyHistory();
            return false;
        }
    }

    return true;
}

void ReconstructionSDLGPU::destroyHistory()
{
    SDL_GPUDevice *device = SDLGPU::device();
    if (device) {
        for (std::uint32_t index = 0u; index < 2u; ++index) {
            if (history_age_[index]) SDL_ReleaseGPUTexture(device, history_age_[index]);
            if (history_surface_[index]) SDL_ReleaseGPUTexture(device, history_surface_[index]);
            if (history_depth_[index]) SDL_ReleaseGPUTexture(device, history_depth_[index]);
            if (history_color_[index]) SDL_ReleaseGPUTexture(device, history_color_[index]);
        }
    }
    history_color_.fill(nullptr);
    history_depth_.fill(nullptr);
    history_surface_.fill(nullptr);
    history_age_.fill(nullptr);
    history_valid_ = false;
    history_index_ = 0u;
}

bool ReconstructionSDLGPU::resize(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (!ensurePipeline()) return false;
    if (width == width_ && height == height_ && history_color_[0] && history_color_[1])
        return true;

    destroyHistory();
    width_ = width;
    height_ = height;
    if (!createHistory()) return false;
    reset();
    return true;
}

void ReconstructionSDLGPU::reset()
{
    history_valid_ = false;
    history_index_ = 0u;
    previous_camera_ = {};
}

void ReconstructionSDLGPU::shutdown()
{
    destroyHistory();
    SDL_GPUDevice *device = SDLGPU::device();
    if (device) {
        if (nearest_sampler_) SDL_ReleaseGPUSampler(device, nearest_sampler_);
        if (pipeline_) SDL_ReleaseGPUComputePipeline(device, pipeline_);
    }
    nearest_sampler_ = nullptr;
    pipeline_ = nullptr;
    previous_camera_ = {};
    width_ = 1;
    height_ = 1;
}

bool ReconstructionSDLGPU::resolve(
    SDL_GPUCommandBuffer *command,
    SDL_GPUTexture *fresh_color,
    SDL_GPUTexture *fresh_depth,
    SDL_GPUTexture *fresh_surface,
    SDL_GPUTexture *output_color,
    SDL_GPUTexture *output_depth,
    const Scenes::CameraState& camera,
    const Reconstruction::Settings& settings,
    std::uint32_t frame_index,
    std::uint32_t grid,
    bool camera_moving,
    bool reset_history)
{
    if (!command || !fresh_color || !fresh_depth || !fresh_surface ||
        !output_color || !output_depth || !camera.valid)
        return false;
    if (!ensurePipeline()) return false;
    if ((!history_color_[0] || !history_color_[1]) && !createHistory()) return false;

    if (reset_history) reset();
    grid = std::clamp<std::uint32_t>(grid, 1u, 8u);

    ReconstructionUniforms uniforms{};
    encodeCamera(
        camera,
        width_,
        height_,
        uniforms.current_position_near,
        uniforms.current_forward_far,
        uniforms.current_right_aspect,
        uniforms.current_up_tan_half_fov,
        uniforms.current_projection_alpha);

    const Scenes::CameraState& previous = history_valid_ ? previous_camera_ : camera;
    encodeCamera(
        previous,
        width_,
        height_,
        uniforms.previous_position_near,
        uniforms.previous_forward_far,
        uniforms.previous_right_aspect,
        uniforms.previous_up_tan_half_fov,
        uniforms.previous_projection_alpha);

    uniforms.resolution_grid_phase = {
        static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_),
        grid,
        Reconstruction::phaseIndex(frame_index, grid),
    };
    uniforms.control = {
        settings.temporal_reuse ? 1u : 0u,
        history_valid_ ? 1u : 0u,
        camera_moving ? 1u : 0u,
        settings.debug_reconstruction ? 1u : 0u,
    };
    uniforms.params = {
        static_cast<float>(std::max(settings.maximum_history, 1u)),
        0.03f,
        0.75f,
        0.0f,
    };
    SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

    const std::uint32_t read_index = history_index_;
    const std::uint32_t write_index = 1u - read_index;
    const SDL_GPUTextureSamplerBinding sampled[7] = {
        {fresh_color, nearest_sampler_},
        {fresh_depth, nearest_sampler_},
        {fresh_surface, nearest_sampler_},
        {history_color_[read_index], nearest_sampler_},
        {history_depth_[read_index], nearest_sampler_},
        {history_surface_[read_index], nearest_sampler_},
        {history_age_[read_index], nearest_sampler_},
    };

    SDL_GPUStorageTextureReadWriteBinding writable[6]{};
    writable[0].texture = output_color;
    writable[1].texture = output_depth;
    writable[2].texture = history_color_[write_index];
    writable[3].texture = history_depth_[write_index];
    writable[4].texture = history_surface_[write_index];
    writable[5].texture = history_age_[write_index];

    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
        command,
        writable,
        6u,
        nullptr,
        0u);
    if (!pass) return false;

    SDL_BindGPUComputePipeline(pass, pipeline_);
    SDL_BindGPUComputeSamplers(pass, 0u, sampled, 7u);
    SDL_DispatchGPUCompute(
        pass,
        (static_cast<Uint32>(width_) + 7u) / 8u,
        (static_cast<Uint32>(height_) + 7u) / 8u,
        1u);
    SDL_EndGPUComputePass(pass);

    previous_camera_ = camera;
    history_index_ = write_index;
    history_valid_ = true;
    return true;
}

} // namespace Renderer::Internal
