#include "Renderer/Internal/OcclusionCullingSDLGPU.hpp"

#include "Animation/Animation.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Internal/ModelGeometry.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/SDLGPU/Context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::Visibility::SDLGPU {
namespace {

constexpr std::size_t MaximumHiZLevels = 16u;

inline constexpr const char *OcclusionShader = R"HLSL(
struct GpuBounds {
    float4 minimum;
    float4 maximum;
    uint4 meta;
};

StructuredBuffer<GpuBounds> Bounds : register(t0, space0);
RWStructuredBuffer<uint> Visibility : register(u0, space1);

Texture2D<float> HiZ0  : register(t0,  space2); SamplerState Samp0  : register(s0,  space2);
Texture2D<float> HiZ1  : register(t1,  space2); SamplerState Samp1  : register(s1,  space2);
Texture2D<float> HiZ2  : register(t2,  space2); SamplerState Samp2  : register(s2,  space2);
Texture2D<float> HiZ3  : register(t3,  space2); SamplerState Samp3  : register(s3,  space2);
Texture2D<float> HiZ4  : register(t4,  space2); SamplerState Samp4  : register(s4,  space2);
Texture2D<float> HiZ5  : register(t5,  space2); SamplerState Samp5  : register(s5,  space2);
Texture2D<float> HiZ6  : register(t6,  space2); SamplerState Samp6  : register(s6,  space2);
Texture2D<float> HiZ7  : register(t7,  space2); SamplerState Samp7  : register(s7,  space2);
Texture2D<float> HiZ8  : register(t8,  space2); SamplerState Samp8  : register(s8,  space2);
Texture2D<float> HiZ9  : register(t9,  space2); SamplerState Samp9  : register(s9,  space2);
Texture2D<float> HiZ10 : register(t10, space2); SamplerState Samp10 : register(s10, space2);
Texture2D<float> HiZ11 : register(t11, space2); SamplerState Samp11 : register(s11, space2);
Texture2D<float> HiZ12 : register(t12, space2); SamplerState Samp12 : register(s12, space2);
Texture2D<float> HiZ13 : register(t13, space2); SamplerState Samp13 : register(s13, space2);
Texture2D<float> HiZ14 : register(t14, space2); SamplerState Samp14 : register(s14, space2);
Texture2D<float> HiZ15 : register(t15, space2); SamplerState Samp15 : register(s15, space2);

cbuffer FrameData : register(b0, space3) {
    float4 CameraPositionNear;
    float4 CameraForwardFar;
    float4 CameraRightAspect;
    float4 CameraUpTanHalfFov;
    float4 ProjectionAlpha;
    float4 Resolution;
    int4 Counts;
    uint4 Frame;
    uint4 PathPolicy;
};

cbuffer VisibilityData : register(b1, space3) {
    uint4 Info;
    float4 Params;
};

float SampleHiZ(uint level, float2 uv)
{
    uv = saturate(uv);
    if (level == 0u) return HiZ0.SampleLevel(Samp0, uv, 0.0).r;
    if (level == 1u) return HiZ1.SampleLevel(Samp1, uv, 0.0).r;
    if (level == 2u) return HiZ2.SampleLevel(Samp2, uv, 0.0).r;
    if (level == 3u) return HiZ3.SampleLevel(Samp3, uv, 0.0).r;
    if (level == 4u) return HiZ4.SampleLevel(Samp4, uv, 0.0).r;
    if (level == 5u) return HiZ5.SampleLevel(Samp5, uv, 0.0).r;
    if (level == 6u) return HiZ6.SampleLevel(Samp6, uv, 0.0).r;
    if (level == 7u) return HiZ7.SampleLevel(Samp7, uv, 0.0).r;
    if (level == 8u) return HiZ8.SampleLevel(Samp8, uv, 0.0).r;
    if (level == 9u) return HiZ9.SampleLevel(Samp9, uv, 0.0).r;
    if (level == 10u) return HiZ10.SampleLevel(Samp10, uv, 0.0).r;
    if (level == 11u) return HiZ11.SampleLevel(Samp11, uv, 0.0).r;
    if (level == 12u) return HiZ12.SampleLevel(Samp12, uv, 0.0).r;
    if (level == 13u) return HiZ13.SampleLevel(Samp13, uv, 0.0).r;
    if (level == 14u) return HiZ14.SampleLevel(Samp14, uv, 0.0).r;
    return HiZ15.SampleLevel(Samp15, uv, 0.0).r;
}

bool ProjectCorner(float3 position, out float2 uv, out float depth)
{
    float3 delta = position - CameraPositionNear.xyz;
    float forward_depth = dot(delta, CameraForwardFar.xyz);
    float near_plane = max(CameraPositionNear.w, 1.0e-5);
    if (forward_depth <= near_plane) return false;

    float x;
    float y;
    if (ProjectionAlpha.z > 0.5) {
        x = dot(delta, CameraRightAspect.xyz) / max(abs(ProjectionAlpha.x), 1.0e-6);
        y = dot(delta, CameraUpTanHalfFov.xyz) / max(abs(ProjectionAlpha.y), 1.0e-6);
        float far_plane = max(CameraForwardFar.w, near_plane + 1.0e-5);
        depth = saturate((forward_depth - near_plane) / (far_plane - near_plane));
    } else {
        float tan_half_fov = max(CameraUpTanHalfFov.w, 1.0e-6);
        float aspect = max(CameraRightAspect.w, 1.0e-6);
        x = dot(delta, CameraRightAspect.xyz) /
            max(tan_half_fov * aspect * forward_depth, 1.0e-6);
        y = dot(delta, CameraUpTanHalfFov.xyz) /
            max(tan_half_fov * forward_depth, 1.0e-6);

        float far_plane = CameraForwardFar.w;
        if (far_plane < 3.0e37) {
            float clip_z = (far_plane * forward_depth - near_plane * far_plane) /
                max(far_plane - near_plane, 1.0e-5);
            depth = saturate(clip_z / forward_depth);
        } else {
            depth = saturate(1.0 - near_plane / forward_depth);
        }
    }

    uv = float2(x * 0.5 + 0.5, 0.5 - y * 0.5);
    return all(isfinite(float3(uv, depth)));
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Info.x) return;

    GpuBounds bounds = Bounds[id.x];
    uint entity = bounds.meta.x;
    if (Visibility[entity] == 0u || Info.y == 0u) return;

    float3 minimum = bounds.minimum.xyz;
    float3 maximum = bounds.maximum.xyz;
    float3 corners[8] = {
        float3(minimum.x, minimum.y, minimum.z),
        float3(maximum.x, minimum.y, minimum.z),
        float3(maximum.x, maximum.y, minimum.z),
        float3(minimum.x, maximum.y, minimum.z),
        float3(minimum.x, minimum.y, maximum.z),
        float3(maximum.x, minimum.y, maximum.z),
        float3(maximum.x, maximum.y, maximum.z),
        float3(minimum.x, maximum.y, maximum.z)
    };

    float2 uv_min = float2(1.0, 1.0);
    float2 uv_max = float2(0.0, 0.0);
    float nearest_depth = 1.0;
    for (uint corner = 0u; corner < 8u; ++corner) {
        float2 uv;
        float depth;
        if (!ProjectCorner(corners[corner], uv, depth)) {
            Visibility[entity] = 1u;
            return;
        }
        uv_min = min(uv_min, uv);
        uv_max = max(uv_max, uv);
        nearest_depth = min(nearest_depth, depth);
    }

    if (uv_max.x < 0.0 || uv_max.y < 0.0 || uv_min.x > 1.0 || uv_min.y > 1.0) {
        Visibility[entity] = 1u;
        return;
    }

    uv_min = saturate(uv_min);
    uv_max = saturate(uv_max);
    float2 pixel_extent = max((uv_max - uv_min) * float2(Info.zw), float2(1.0, 1.0));
    float maximum_extent = max(pixel_extent.x, pixel_extent.y);
    uint level = (uint)floor(log2(max(maximum_extent, 1.0)));
    level = min(level, Info.y - 1u);

    float farthest_occluder = SampleHiZ(level, uv_min);
    farthest_occluder = max(farthest_occluder, SampleHiZ(level, float2(uv_max.x, uv_min.y)));
    farthest_occluder = max(farthest_occluder, SampleHiZ(level, float2(uv_min.x, uv_max.y)));
    farthest_occluder = max(farthest_occluder, SampleHiZ(level, uv_max));

    Visibility[entity] = nearest_depth > farthest_occluder + Params.x ? 0u : 1u;
}
)HLSL";

struct CpuBounds {
    Vec3 minimum{};
    Vec3 maximum{};
    bool valid = false;
    bool skip = false;
};

struct alignas(16) VisibilityUniforms {
    std::uint32_t candidate_count = 0u;
    std::uint32_t level_count = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    float depth_bias = 0.00025f;
    float reserved0 = 0.0f;
    float reserved1 = 0.0f;
    float reserved2 = 0.0f;
};

static_assert(sizeof(VisibilityUniforms) == 32u);

bool fail(std::string *error, const char *message)
{
    if (error) *error = message;
    return false;
}

bool sameGeometryRevision(
    const Scenes::Scene::RenderRevision& a,
    const Scenes::Scene::RenderRevision& b)
{
    return a.structure == b.structure &&
        a.transform == b.transform &&
        a.resource == b.resource &&
        a.animation == b.animation &&
        a.model_resource == b.model_resource;
}

bool worldBounds(const Scenes::Scene::RenderItem& item, Vec3 *minimum, Vec3 *maximum)
{
    if (!minimum || !maximum || !item.mesh || !item.transform || item.mesh->vertices.empty())
        return false;

    const auto& source = item.mesh->bounds;
    const std::array<Vec3, 8> local{{
        {source.minimum.x, source.minimum.y, source.minimum.z},
        {source.maximum.x, source.minimum.y, source.minimum.z},
        {source.maximum.x, source.maximum.y, source.minimum.z},
        {source.minimum.x, source.maximum.y, source.minimum.z},
        {source.minimum.x, source.minimum.y, source.maximum.z},
        {source.maximum.x, source.minimum.y, source.maximum.z},
        {source.maximum.x, source.maximum.y, source.maximum.z},
        {source.minimum.x, source.maximum.y, source.maximum.z},
    }};

    const Math::Mat4 model = Math::modelMatrix(*item.transform);
    *minimum = Math::transformPoint(model, local[0]);
    *maximum = *minimum;
    for (std::size_t index = 1u; index < local.size(); ++index) {
        const Vec3 point = Math::transformPoint(model, local[index]);
        minimum->x = std::min(minimum->x, point.x);
        minimum->y = std::min(minimum->y, point.y);
        minimum->z = std::min(minimum->z, point.z);
        maximum->x = std::max(maximum->x, point.x);
        maximum->y = std::max(maximum->y, point.y);
        maximum->z = std::max(maximum->z, point.z);
    }
    return true;
}

} // namespace

struct alignas(16) OcclusionCulling::GpuBounds {
    std::array<float, 4> minimum{};
    std::array<float, 4> maximum{};
    std::array<std::uint32_t, 4> meta{};
};

static_assert(sizeof(OcclusionCulling::GpuBounds) == 48u);

OcclusionCulling::~OcclusionCulling()
{
    clear();
}

bool OcclusionCulling::init()
{
    if (pipeline_ && sampler_) return true;
    if (!Renderer::SDLGPU::device()) return false;

    if (!pipeline_)
        pipeline_ = Renderer::SDLGPU::compileComputePipeline(
            OcclusionShader,
            "Horse Hi-Z Occlusion",
            "main"
        );
    if (!sampler_) sampler_ = Renderer::SDLGPU::createNearestSampler();

    if (!pipeline_ || !sampler_) {
        clear();
        return false;
    }
    return true;
}

bool OcclusionCulling::sync(
    const Ecs::World& world,
    const Scenes::SceneCache& scene,
    std::string *error)
{
    if (error) error->clear();
    if (!init()) return fail(error, "failed to initialize GPU occlusion culling");

    const Scenes::Scene::RenderRevision current = Scenes::Scene::renderRevision(world);
    if (revision_valid_ && world_ == &world && sameGeometryRevision(current, revision_))
        return true;

    std::size_t entity_count = 1u;
    for (const Ecs::Entity entity : world.entities())
        entity_count = std::max(entity_count, static_cast<std::size_t>(entity) + 1u);
    std::vector<CpuBounds> per_entity(entity_count);

    for (const Scenes::Scene::RenderItem& item : scene.renderItems()) {
        const std::size_t entity = static_cast<std::size_t>(item.entity);
        if (entity >= per_entity.size()) continue;
        CpuBounds& target = per_entity[entity];

        const bool dynamic_geometry =
            world.has<Internal::ModelDeformComponent>(item.entity) ||
            world.has<Animation::SkinBindingComponent>(item.entity);
        const bool hierarchical = world.has<Parent>(item.entity);
        if (item.layer != RenderLayer::World || dynamic_geometry || hierarchical) {
            target.skip = true;
            target.valid = false;
            continue;
        }
        if (target.skip) continue;

        Vec3 minimum{};
        Vec3 maximum{};
        if (!worldBounds(item, &minimum, &maximum)) {
            target.skip = true;
            target.valid = false;
            continue;
        }

        if (!target.valid) {
            target.minimum = minimum;
            target.maximum = maximum;
            target.valid = true;
        } else {
            target.minimum.x = std::min(target.minimum.x, minimum.x);
            target.minimum.y = std::min(target.minimum.y, minimum.y);
            target.minimum.z = std::min(target.minimum.z, minimum.z);
            target.maximum.x = std::max(target.maximum.x, maximum.x);
            target.maximum.y = std::max(target.maximum.y, maximum.y);
            target.maximum.z = std::max(target.maximum.z, maximum.z);
        }
    }

    bounds_.clear();
    for (std::size_t entity = 0u; entity < per_entity.size(); ++entity) {
        const CpuBounds& source = per_entity[entity];
        if (!source.valid || source.skip) continue;
        if (entity > std::numeric_limits<std::uint32_t>::max())
            return fail(error, "occlusion entity index exceeds GPU range");

        GpuBounds target;
        target.minimum = {source.minimum.x, source.minimum.y, source.minimum.z, 0.0f};
        target.maximum = {source.maximum.x, source.maximum.y, source.maximum.z, 0.0f};
        target.meta = {static_cast<std::uint32_t>(entity), 0u, 0u, 0u};
        bounds_.push_back(target);
    }

    if (bounds_.size() > std::numeric_limits<std::uint32_t>::max())
        return fail(error, "occlusion candidate count exceeds GPU range");

    const std::size_t bytes = bounds_.size() * sizeof(GpuBounds);
    if (bytes != 0u) {
        if (!bounds_buffer_ || bounds_capacity_ < bytes) {
            SDL_GPUBuffer *replacement = Renderer::SDLGPU::createBuffer(
                SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
                bytes,
                bounds_.data(),
                "Horse Occlusion Bounds"
            );
            if (!replacement) return fail(error, "failed to allocate occlusion bounds buffer");
            if (bounds_buffer_) SDL_ReleaseGPUBuffer(Renderer::SDLGPU::device(), bounds_buffer_);
            bounds_buffer_ = replacement;
            bounds_capacity_ = bytes;
        } else {
            SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(Renderer::SDLGPU::device());
            if (!command || !Renderer::SDLGPU::uploadBuffer(
                    command,
                    bounds_buffer_,
                    bounds_.data(),
                    bytes,
                    true) ||
                !SDL_SubmitGPUCommandBuffer(command))
            {
                if (command) SDL_CancelGPUCommandBuffer(command);
                return fail(error, "failed to upload occlusion bounds buffer");
            }
        }
    }

    world_ = &world;
    revision_ = current;
    revision_valid_ = true;
    return true;
}

bool OcclusionCulling::cull(
    SDL_GPUCommandBuffer *command,
    const HiZPyramid& hi_z,
    SDL_GPUBuffer *visibility,
    const Renderer::SDLGPU::FrameUniforms& frame,
    std::uint32_t width,
    std::uint32_t height)
{
    if (bounds_.empty()) return true;
    if (!command || !visibility || !bounds_buffer_ || !pipeline_ || !sampler_ || !hi_z.ready())
        return false;

    const std::size_t usable_levels = std::min(hi_z.levelCount(), MaximumHiZLevels);
    if (usable_levels == 0u) return true;

    const VisibilityUniforms uniforms{
        static_cast<std::uint32_t>(bounds_.size()),
        static_cast<std::uint32_t>(usable_levels),
        std::max(width, 1u),
        std::max(height, 1u),
        0.00025f,
        0.0f,
        0.0f,
        0.0f,
    };
    SDL_PushGPUComputeUniformData(command, 0u, &frame, sizeof(frame));
    SDL_PushGPUComputeUniformData(command, 1u, &uniforms, sizeof(uniforms));

    SDL_GPUStorageBufferReadWriteBinding writable{};
    writable.buffer = visibility;
    writable.cycle = false;
    SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(
        command,
        nullptr,
        0u,
        &writable,
        1u
    );
    if (!pass) return false;

    SDL_BindGPUComputePipeline(pass, pipeline_);
    SDL_GPUBuffer *read_buffers[] = {bounds_buffer_};
    SDL_BindGPUComputeStorageBuffers(pass, 0u, read_buffers, 1u);

    std::array<SDL_GPUTextureSamplerBinding, MaximumHiZLevels> samplers{};
    SDL_GPUTexture *fallback = hi_z.level(usable_levels - 1u);
    for (std::size_t index = 0u; index < samplers.size(); ++index) {
        SDL_GPUTexture *texture = index < usable_levels ? hi_z.level(index) : fallback;
        samplers[index] = {texture, sampler_};
    }
    SDL_BindGPUComputeSamplers(
        pass,
        0u,
        samplers.data(),
        static_cast<Uint32>(samplers.size())
    );

    SDL_DispatchGPUCompute(
        pass,
        (static_cast<Uint32>(bounds_.size()) + 63u) / 64u,
        1u,
        1u
    );
    SDL_EndGPUComputePass(pass);
    return true;
}

void OcclusionCulling::clear()
{
    SDL_GPUDevice *device = Renderer::SDLGPU::device();
    if (device) {
        if (bounds_buffer_) SDL_ReleaseGPUBuffer(device, bounds_buffer_);
        if (sampler_) SDL_ReleaseGPUSampler(device, sampler_);
        if (pipeline_) SDL_ReleaseGPUComputePipeline(device, pipeline_);
    }
    bounds_buffer_ = nullptr;
    bounds_capacity_ = 0u;
    sampler_ = nullptr;
    pipeline_ = nullptr;
    bounds_.clear();
    world_ = nullptr;
    revision_ = {};
    revision_valid_ = false;
}

std::size_t OcclusionCulling::candidateCount() const
{
    return bounds_.size();
}

} // namespace Renderer::Visibility::SDLGPU
