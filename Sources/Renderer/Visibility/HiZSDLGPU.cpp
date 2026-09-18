#include "Renderer/Internal/HiZSDLGPU.hpp"

#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <cstdint>

namespace Renderer::Visibility::SDLGPU {
namespace {

inline constexpr const char *HiZShader = R"HLSL(
Texture2D<float> Source : register(t0, space0);
SamplerState SourceSampler : register(s0, space0);
RWTexture2D<float> Output : register(u0, space1);

cbuffer HiZData : register(b0, space2) {
    uint4 Size;
    uint4 Control;
};

float ReadDepth(uint2 pixel)
{
    uint2 source_size = max(Size.xy, uint2(1u, 1u));
    uint2 clamped = min(pixel, source_size - uint2(1u, 1u));
    float2 uv = (float2(clamped) + float2(0.5, 0.5)) / float2(source_size);
    return Source.SampleLevel(SourceSampler, uv, 0.0).r;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint2 output_size = Size.zw;
    if (any(id.xy >= output_size)) return;

    if (Control.x == 0u) {
        Output[id.xy] = ReadDepth(id.xy);
        return;
    }

    uint2 base = id.xy * 2u;
    float depth = ReadDepth(base);
    depth = max(depth, ReadDepth(base + uint2(1u, 0u)));
    depth = max(depth, ReadDepth(base + uint2(0u, 1u)));
    depth = max(depth, ReadDepth(base + uint2(1u, 1u)));
    Output[id.xy] = depth;
}
)HLSL";

struct alignas(16) HiZUniforms {
    std::uint32_t source_width = 0u;
    std::uint32_t source_height = 0u;
    std::uint32_t output_width = 0u;
    std::uint32_t output_height = 0u;
    std::uint32_t reduce = 0u;
    std::uint32_t reserved0 = 0u;
    std::uint32_t reserved1 = 0u;
    std::uint32_t reserved2 = 0u;
};

static_assert(sizeof(HiZUniforms) == 32u);

constexpr SDL_GPUTextureUsageFlags HiZUsage =
    SDL_GPU_TEXTUREUSAGE_SAMPLER |
    SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;

} // namespace

HiZPyramid::~HiZPyramid()
{
    clear();
}

bool HiZPyramid::init()
{
    if (pipeline_ && sampler_) return true;
    if (!Renderer::SDLGPU::device()) return false;

    if (!pipeline_)
        pipeline_ = Renderer::SDLGPU::compileComputePipeline(
            HiZShader,
            "Horse Hi-Z",
            "main"
        );
    if (!sampler_) sampler_ = Renderer::SDLGPU::createNearestSampler();

    if (!pipeline_ || !sampler_) {
        clear();
        return false;
    }
    return true;
}

void HiZPyramid::clearLevels()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        for (SDL_GPUTexture *texture : levels_) {
            if (texture) SDL_ReleaseGPUTexture(device, texture);
        }
    }
    levels_.clear();
    width_ = 0u;
    height_ = 0u;
}

bool HiZPyramid::resize(std::uint32_t width, std::uint32_t height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (!init()) return false;

    const std::size_t requested_levels = HiZ::mipCount(width, height);
    if (width_ == width && height_ == height && levels_.size() == requested_levels)
        return true;

    clearLevels();
    width_ = width;
    height_ = height;
    levels_.reserve(requested_levels);

    for (std::size_t index = 0u; index < requested_levels; ++index) {
        const HiZ::Extent size = HiZ::mipExtent(
            width,
            height,
            static_cast<std::uint32_t>(index)
        );
        SDL_GPUTexture *texture = Renderer::SDLGPU::createTexture(
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            HiZUsage,
            size.width,
            size.height,
            "Horse Hi-Z Level"
        );
        if (!texture) {
            clearLevels();
            return false;
        }
        levels_.push_back(texture);
    }
    return true;
}

bool HiZPyramid::build(SDL_GPUCommandBuffer *command, SDL_GPUTexture *depth)
{
    if (!command || !depth || !pipeline_ || !sampler_ || levels_.empty()) return false;

    SDL_GPUTexture *source = depth;
    HiZ::Extent source_size{width_, height_};
    for (std::size_t index = 0u; index < levels_.size(); ++index) {
        const HiZ::Extent output_size = extent(index);
        const HiZUniforms uniforms{
            source_size.width,
            source_size.height,
            output_size.width,
            output_size.height,
            index == 0u ? 0u : 1u,
            0u,
            0u,
            0u,
        };
        SDL_PushGPUComputeUniformData(command, 0u, &uniforms, sizeof uniforms);

        SDL_GPUStorageTextureReadWriteBinding writable{};
        writable.texture = levels_[index];
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
            command,
            &writable,
            1u,
            nullptr,
            0u
        );
        if (!pass) return false;

        SDL_BindGPUComputePipeline(pass, pipeline_);
        const SDL_GPUTextureSamplerBinding sampled{source, sampler_};
        SDL_BindGPUComputeSamplers(pass, 0u, &sampled, 1u);
        SDL_DispatchGPUCompute(
            pass,
            (output_size.width + 7u) / 8u,
            (output_size.height + 7u) / 8u,
            1u
        );
        SDL_EndGPUComputePass(pass);

        source = levels_[index];
        source_size = output_size;
    }
    return true;
}

void HiZPyramid::clear()
{
    clearLevels();
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (sampler_) SDL_ReleaseGPUSampler(device, sampler_);
        if (pipeline_) SDL_ReleaseGPUComputePipeline(device, pipeline_);
    }
    sampler_ = nullptr;
    pipeline_ = nullptr;
}

bool HiZPyramid::ready() const
{
    return pipeline_ && sampler_ && !levels_.empty();
}

std::size_t HiZPyramid::levelCount() const
{
    return levels_.size();
}

SDL_GPUTexture *HiZPyramid::level(std::size_t index) const
{
    return index < levels_.size() ? levels_[index] : nullptr;
}

HiZ::Extent HiZPyramid::extent(std::size_t index) const
{
    if (index >= levels_.size()) return {};
    return HiZ::mipExtent(width_, height_, static_cast<std::uint32_t>(index));
}

} // namespace Renderer::Visibility::SDLGPU
