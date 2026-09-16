#include "Renderer/Rasterizer/ShadowMapsSDLGPU.hpp"

#include "Renderer/Math.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/SDLGPU/Shaders.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::RasterizerSDLGPU {
namespace {

using Float4 = std::array<float, 4>;

constexpr std::size_t MaximumShadowLayers = 255u;
constexpr float Pi = 3.14159265358979323846f;
constexpr float CascadeSplitBlend = 0.5f;

struct ShadowView {
    Float4 position_near{};
    Float4 forward_far{};
    Float4 right_scale{};
    Float4 up_scale{};
    Float4 meta{};
};

struct ShadowLight {
    Float4 data{};
};

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 multiply(Vec3 value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

void basis(Vec3 direction, Vec3 *right, Vec3 *up)
{
    const Vec3 forward = Math::normalize(direction);
    const Vec3 helper = std::abs(Math::dot(forward, {0.0f, 1.0f, 0.0f})) > 0.95f
        ? Vec3{1.0f, 0.0f, 0.0f}
        : Vec3{0.0f, 1.0f, 0.0f};
    *right = Math::normalize(Math::cross(forward, helper));
    *up = Math::normalize(Math::cross(*right, forward));
}

ShadowView perspectiveView(
    Vec3 position,
    Vec3 forward,
    float near_plane,
    float far_plane,
    float fov_degrees,
    float aspect,
    std::size_t layer,
    float cascade_far = 0.0f)
{
    Vec3 right{};
    Vec3 up{};
    forward = Math::normalize(forward);
    basis(forward, &right, &up);
    const float tangent = std::tan(
        std::clamp(fov_degrees, 1.0f, 179.0f) * Pi / 360.0f);
    return {
        {position.x, position.y, position.z, near_plane},
        {forward.x, forward.y, forward.z, far_plane},
        {right.x, right.y, right.z, std::max(aspect, 1.0e-6f)},
        {up.x, up.y, up.z, std::max(tangent, 1.0e-6f)},
        {0.0f, static_cast<float>(layer), cascade_far, 0.0f},
    };
}

ShadowView orthographicView(
    Vec3 position,
    Vec3 forward,
    float near_plane,
    float far_plane,
    float half_width,
    float half_height,
    std::size_t layer,
    float cascade_far)
{
    Vec3 right{};
    Vec3 up{};
    forward = Math::normalize(forward);
    basis(forward, &right, &up);
    return {
        {position.x, position.y, position.z, near_plane},
        {forward.x, forward.y, forward.z, far_plane},
        {right.x, right.y, right.z, std::max(half_width, 1.0e-4f)},
        {up.x, up.y, up.z, std::max(half_height, 1.0e-4f)},
        {1.0f, static_cast<float>(layer), cascade_far, 0.0f},
    };
}

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

SDL_GPUTextureFormat shadowDepthFormat()
{
    SDL_GPUDevice *device = SDLGPU::device();
    if (!device) return SDL_GPU_TEXTUREFORMAT_INVALID;
    constexpr SDL_GPUTextureUsageFlags usage =
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
        SDL_GPU_TEXTUREUSAGE_SAMPLER;
    constexpr std::array<SDL_GPUTextureFormat, 3> formats {{
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    }};
    for (const SDL_GPUTextureFormat format : formats) {
        if (SDL_GPUTextureSupportsFormat(device, format, SDL_GPU_TEXTURETYPE_2D_ARRAY, usage))
            return format;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

SDL_GPUSampler *createShadowSampler()
{
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.enable_compare = true;
    return SDL_CreateGPUSampler(SDLGPU::device(), &info);
}

SDL_GPUGraphicsPipeline *createShadowPipeline(SDL_GPUTextureFormat depth_format)
{
    SDL_GPUShader *vertex = SDLGPU::compileGraphicsShader(
        SDLGPU::Shaders::Shadow,
        SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
        "Horse Shadow VS",
        "VSMain");
    SDL_GPUShader *fragment = SDLGPU::compileGraphicsShader(
        SDLGPU::Shaders::Shadow,
        SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
        "Horse Shadow PS",
        "PSMain");
    if (!vertex || !fragment) {
        if (vertex) SDL_ReleaseGPUShader(SDLGPU::device(), vertex);
        if (fragment) SDL_ReleaseGPUShader(SDLGPU::device(), fragment);
        return nullptr;
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.rasterizer_state.depth_bias_constant_factor = 1.25f;
    info.rasterizer_state.depth_bias_slope_factor = 1.75f;
    info.rasterizer_state.enable_depth_bias = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.target_info.depth_stencil_format = depth_format;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(SDLGPU::device(), &info);
    SDL_ReleaseGPUShader(SDLGPU::device(), vertex);
    SDL_ReleaseGPUShader(SDLGPU::device(), fragment);
    return pipeline;
}

} // namespace

struct ShadowMaps::Impl {
    SDL_GPUTexture *texture = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    SDL_GPUBuffer *data_buffer = nullptr;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUTextureFormat depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    std::size_t data_capacity = 0u;
    std::vector<ShadowView> views;
    std::vector<ShadowLight> lights;
    std::vector<Float4> data;
    std::uint64_t signature = std::numeric_limits<std::uint64_t>::max();
    int resolution = 0;
    std::size_t layers = 0u;

    bool ensureTexture(int requested_resolution, std::size_t requested_layers)
    {
        const int next_resolution = std::max(requested_resolution, 1);
        const std::size_t next_layers = std::max<std::size_t>(requested_layers, 1u);
        if (texture && resolution == next_resolution && layers == next_layers) return true;

        if (texture) SDL_ReleaseGPUTexture(SDLGPU::device(), texture);
        texture = nullptr;
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
        info.format = depth_format;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(next_resolution);
        info.height = static_cast<Uint32>(next_resolution);
        info.layer_count_or_depth = static_cast<Uint32>(next_layers);
        info.num_levels = 1u;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        texture = SDL_CreateGPUTexture(SDLGPU::device(), &info);
        if (!texture) return false;
        resolution = next_resolution;
        layers = next_layers;
        signature = std::numeric_limits<std::uint64_t>::max();
        return true;
    }

    bool ensureDataBuffer(std::size_t bytes)
    {
        if (data_buffer && data_capacity >= bytes) return true;
        SDL_GPUBuffer *replacement = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            std::max<std::size_t>(bytes, sizeof(Float4)),
            nullptr,
            "Horse Raster Shadows");
        if (!replacement) return false;
        if (data_buffer) SDL_ReleaseGPUBuffer(SDLGPU::device(), data_buffer);
        data_buffer = replacement;
        data_capacity = std::max<std::size_t>(bytes, sizeof(Float4));
        return true;
    }

    bool appendView(const ShadowView& view)
    {
        if (views.size() >= MaximumShadowLayers) return false;
        views.push_back(view);
        return true;
    }

    void appendPointLight(const Scenes::LightState& light, const ShadowMapSettings& settings)
    {
        static constexpr std::array<Vec3, 6> directions {{
            { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
            { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
            { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
        }};
        const float near_plane = std::max(settings.near_plane, 1.0e-4f);
        const float far_plane = light.range > near_plane
            ? light.range
            : std::max(settings.distance, near_plane + 1.0f);
        for (const Vec3 direction : directions) {
            if (!appendView(perspectiveView(
                    light.position,
                    direction,
                    near_plane,
                    far_plane,
                    90.0f,
                    1.0f,
                    views.size())))
                break;
        }
    }

    void appendSpotLight(const Scenes::LightState& light, const ShadowMapSettings& settings)
    {
        const float near_plane = std::max(settings.near_plane, 1.0e-4f);
        const float far_plane = light.range > near_plane
            ? light.range
            : std::max(settings.distance, near_plane + 1.0f);
        appendView(perspectiveView(
            light.position,
            light.direction,
            near_plane,
            far_plane,
            std::max(light.outer_cone_degrees * 2.0f, 1.0f),
            1.0f,
            views.size()));
    }

    void appendDirectionalLight(
        const Scenes::LightState& light,
        const Scenes::CameraState& camera,
        int viewport_width,
        int viewport_height,
        const ShadowMapSettings& settings)
    {
        if (!camera.valid || views.size() >= MaximumShadowLayers) return;
        const float near_plane = std::max(camera.near_plane, 1.0e-4f);
        float far_plane = std::max(settings.distance, near_plane + 1.0f);
        if (camera.far_plane > near_plane) far_plane = std::min(far_plane, camera.far_plane);
        if (far_plane <= near_plane) return;

        const std::size_t remaining = MaximumShadowLayers - views.size();
        const int cascade_count = std::max(1, std::min(
            settings.cascades,
            static_cast<int>(remaining)));
        const float aspect = camera.aspect_ratio > 1.0e-6f
            ? camera.aspect_ratio
            : static_cast<float>(std::max(viewport_width, 1)) /
                static_cast<float>(std::max(viewport_height, 1));
        const float tangent = std::tan(
            std::clamp(camera.fov_degrees, 1.0f, 179.0f) * Pi / 360.0f);

        float previous = near_plane;
        for (int cascade = 0; cascade < cascade_count; ++cascade) {
            const float fraction = static_cast<float>(cascade + 1) /
                static_cast<float>(cascade_count);
            const float uniform_split = near_plane + (far_plane - near_plane) * fraction;
            const float logarithmic_split = near_plane *
                std::pow(far_plane / near_plane, fraction);
            const float split = uniform_split * (1.0f - CascadeSplitBlend) +
                logarithmic_split * CascadeSplitBlend;
            const float middle = (previous + split) * 0.5f;

            float near_half_width = std::abs(camera.xmag);
            float near_half_height = std::abs(camera.ymag);
            float far_half_width = near_half_width;
            float far_half_height = near_half_height;
            if (camera.projection == Camera::Projection::Perspective) {
                near_half_height = tangent * previous;
                near_half_width = near_half_height * aspect;
                far_half_height = tangent * split;
                far_half_width = far_half_height * aspect;
            }

            const float near_depth = previous - middle;
            const float far_depth = split - middle;
            const float near_radius = std::sqrt(
                near_depth * near_depth +
                near_half_width * near_half_width +
                near_half_height * near_half_height);
            const float far_radius = std::sqrt(
                far_depth * far_depth +
                far_half_width * far_half_width +
                far_half_height * far_half_height);
            const float radius = std::max({near_radius, far_radius, 1.0e-3f});

            Vec3 center = add(camera.position, multiply(camera.forward, middle));
            Vec3 right{};
            Vec3 up{};
            basis(light.direction, &right, &up);
            const float texel = (radius * 2.0f) /
                static_cast<float>(std::max(settings.resolution, 1));
            if (texel > 0.0f) {
                const float x = Math::dot(center, right);
                const float y = Math::dot(center, up);
                center = add(center, multiply(right, std::round(x / texel) * texel - x));
                center = add(center, multiply(up, std::round(y / texel) * texel - y));
            }

            const float shadow_near = std::max(settings.near_plane, 1.0e-4f);
            const Vec3 position = subtract(center, multiply(light.direction, radius * 2.0f));
            const float shadow_far = std::max(radius * 4.0f, shadow_near + 1.0f);
            if (!appendView(orthographicView(
                    position,
                    light.direction,
                    shadow_near,
                    shadow_far,
                    radius,
                    radius,
                    views.size(),
                    split)))
                break;
            previous = split;
        }
    }

    void build(
        const Scenes::CameraState& camera,
        const Lighting::State& lighting,
        int viewport_width,
        int viewport_height,
        const ShadowMapSettings& settings)
    {
        views.clear();
        lights.assign(lighting.lights.size(), ShadowLight{});
        for (std::size_t index = 0u; index < lighting.lights.size(); ++index) {
            const Scenes::LightState& light = lighting.lights[index];
            const std::size_t first = views.size();
            if (light.valid && light.shadows && light.intensity > 0.0f) {
                if (light.type == LightType::Point) appendPointLight(light, settings);
                else if (light.type == LightType::Spot) appendSpotLight(light, settings);
                else appendDirectionalLight(
                    light,
                    camera,
                    viewport_width,
                    viewport_height,
                    settings);
            }
            const std::size_t count = views.size() - first;
            lights[index].data = {
                static_cast<float>(first),
                static_cast<float>(count),
                std::max(light.shadow_bias, 0.0f),
                count > 0u ? 1.0f : 0.0f,
            };
        }

        data.clear();
        data.reserve(1u + lights.size() + views.size() * 5u);
        data.push_back({
            static_cast<float>(lights.size()),
            static_cast<float>(views.size()),
            static_cast<float>(std::max(settings.resolution, 1)),
            0.0f,
        });
        for (const ShadowLight& light : lights) data.push_back(light.data);
        for (const ShadowView& view : views) {
            data.push_back(view.position_near);
            data.push_back(view.forward_far);
            data.push_back(view.right_scale);
            data.push_back(view.up_scale);
            data.push_back(view.meta);
        }
    }

    std::uint64_t currentSignature(
        const RasterGeometry& geometry,
        const Scenes::CameraState& camera,
        const Lighting::State& lighting,
        int viewport_width,
        int viewport_height,
        const ShadowMapSettings& settings) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        hashValue(hash, geometry.shadowRevision());
        hashValue(hash, lighting.revision);
        hashValue(hash, Scenes::cameraSignature(camera));
        hashValue(hash, static_cast<std::uint64_t>(std::max(viewport_width, 1)));
        hashValue(hash, static_cast<std::uint64_t>(std::max(viewport_height, 1)));
        hashValue(hash, static_cast<std::uint64_t>(std::max(settings.resolution, 1)));
        hashValue(hash, static_cast<std::uint64_t>(std::max(settings.cascades, 1)));
        hashFloat(hash, settings.distance);
        hashFloat(hash, settings.near_plane);
        return hash;
    }
};

ShadowMaps::~ShadowMaps()
{
    clear();
    delete impl_;
    impl_ = nullptr;
}

bool ShadowMaps::init(std::string *error)
{
    if (error) error->clear();
    if (!impl_) impl_ = new Impl;
    if (impl_->pipeline && impl_->sampler) return true;
    impl_->depth_format = shadowDepthFormat();
    if (impl_->depth_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        if (error) *error = "no sampleable SDL_GPU depth-array format is available";
        return false;
    }
    impl_->sampler = createShadowSampler();
    impl_->pipeline = createShadowPipeline(impl_->depth_format);
    if (!impl_->sampler || !impl_->pipeline) {
        if (error) *error = "failed to create raster shadow resources";
        clear();
        return false;
    }
    return true;
}

bool ShadowMaps::update(
    SDL_GPUCommandBuffer *command,
    const RasterGeometry& geometry,
    const Scenes::CameraState& camera,
    const Lighting::State& lighting,
    int viewport_width,
    int viewport_height,
    const ShadowMapSettings& settings,
    std::string *error)
{
    if (error) error->clear();
    if (!command || !init(error)) return false;
    const ShadowMapSettings safe_settings {
        std::max(settings.resolution, 1),
        std::max(settings.cascades, 1),
        std::max(settings.distance, 1.0f),
        std::max(settings.near_plane, 1.0e-4f),
    };
    impl_->build(camera, lighting, viewport_width, viewport_height, safe_settings);
    const std::size_t requested_layers = std::max<std::size_t>(impl_->views.size(), 1u);
    const int requested_resolution = impl_->views.empty() ? 1 : safe_settings.resolution;
    if (!impl_->ensureTexture(requested_resolution, requested_layers)) {
        if (error) *error = "failed to create raster shadow-map array";
        return false;
    }

    const std::size_t bytes = impl_->data.size() * sizeof(Float4);
    if (!impl_->ensureDataBuffer(bytes) || !SDLGPU::uploadBuffer(
            command,
            impl_->data_buffer,
            impl_->data.data(),
            bytes,
            true))
    {
        if (error) *error = "failed to upload raster shadow state";
        return false;
    }

    const std::uint64_t next_signature = impl_->currentSignature(
        geometry,
        camera,
        lighting,
        viewport_width,
        viewport_height,
        safe_settings);
    if (impl_->signature == next_signature || impl_->views.empty() || geometry.vertexCount() == 0u) {
        impl_->signature = next_signature;
        return true;
    }

    for (std::size_t index = 0u; index < impl_->views.size(); ++index) {
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = impl_->texture;
        depth.clear_depth = 1.0f;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.layer = static_cast<Uint8>(index);
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(command, nullptr, 0u, &depth);
        if (!pass) {
            if (error) *error = "failed to begin raster shadow pass";
            return false;
        }
        SDL_BindGPUGraphicsPipeline(pass, impl_->pipeline);
        geometry.bind(pass);
        SDL_PushGPUVertexUniformData(
            command,
            0u,
            &impl_->views[index],
            sizeof(ShadowView));
        SDL_DrawGPUPrimitives(
            pass,
            static_cast<Uint32>(std::min<std::size_t>(
                geometry.vertexCount(),
                UINT32_MAX)),
            1u,
            0u,
            0u);
        SDL_EndGPURenderPass(pass);
    }
    impl_->signature = next_signature;
    return true;
}

void ShadowMaps::bind(SDL_GPURenderPass *pass) const
{
    if (!impl_ || !pass || !impl_->texture || !impl_->sampler || !impl_->data_buffer) return;
    const SDL_GPUTextureSamplerBinding binding{impl_->texture, impl_->sampler};
    SDL_BindGPUFragmentSamplers(
        pass,
        static_cast<Uint32>(Scenes::SDLGPU::SceneResources::MaximumTextureSlots - 1u),
        &binding,
        1u);
    SDL_GPUBuffer *buffers[] = {impl_->data_buffer};
    SDL_BindGPUFragmentStorageBuffers(pass, 3u, buffers, 1u);
}

void ShadowMaps::clear()
{
    if (!impl_) return;
    SDL_GPUDevice *device = SDLGPU::device();
    if (device) {
        if (impl_->pipeline) SDL_ReleaseGPUGraphicsPipeline(device, impl_->pipeline);
        if (impl_->sampler) SDL_ReleaseGPUSampler(device, impl_->sampler);
        if (impl_->data_buffer) SDL_ReleaseGPUBuffer(device, impl_->data_buffer);
        if (impl_->texture) SDL_ReleaseGPUTexture(device, impl_->texture);
    }
    impl_->pipeline = nullptr;
    impl_->sampler = nullptr;
    impl_->data_buffer = nullptr;
    impl_->texture = nullptr;
    impl_->depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    impl_->data_capacity = 0u;
    impl_->views.clear();
    impl_->lights.clear();
    impl_->data.clear();
    impl_->signature = std::numeric_limits<std::uint64_t>::max();
    impl_->resolution = 0;
    impl_->layers = 0u;
}

} // namespace Renderer::RasterizerSDLGPU
