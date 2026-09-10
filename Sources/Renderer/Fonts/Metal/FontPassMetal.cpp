#include "Renderer/Fonts/FontPass.hpp"

#ifdef __APPLE__

#include "Models/Images/Image.hpp"
#include "Renderer/Fonts/FontAtlas.hpp"

#include <lwmgl/lwmgl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::Internal {
namespace {

struct MetalUniforms {
    float output_width = 1.0f;
    float output_height = 1.0f;
    float has_depth = 0.0f;
    float padding = 0.0f;
};

struct MetalFontState {
    LWMGLLibrary library = nullptr;
    LWMGLFunction vertex_function = nullptr;
    LWMGLFunction fragment_function = nullptr;
    LWMGLRenderPipeline pipeline = nullptr;
    LWMGLTexture atlas = nullptr;
    LWMGLTexture empty_depth = nullptr;
    LWMGLSampler sampler = nullptr;
    LWMGLBuffer vertices = nullptr;
    LWMGLBuffer uniforms = nullptr;
    std::size_t vertex_capacity = 0u;
};

MetalFontState state;

constexpr const char *shader_source = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FontVertex {
    float4 clip;
    float4 uv_depth;
    float4 color;
};

struct FontOut {
    float4 position [[position]];
    float2 uv;
    float linear_depth;
    float depth_test;
    float4 color;
};

struct FontUniforms {
    float output_width;
    float output_height;
    float has_depth;
    float padding;
};

vertex FontOut font_vertex(
    uint id [[vertex_id]],
    device const FontVertex *vertices [[buffer(0)]])
{
    FontVertex source = vertices[id];
    FontOut out;
    out.position = source.clip;
    out.position.z = (source.clip.z + source.clip.w) * 0.5f;
    out.uv = source.uv_depth.xy;
    out.linear_depth = source.uv_depth.z;
    out.depth_test = source.uv_depth.w;
    out.color = source.color;
    return out;
}

fragment float4 font_fragment(
    FontOut in [[stage_in]],
    constant FontUniforms& uniforms [[buffer(0)]],
    texture2d<float> atlas [[texture(0)]],
    texture2d<float> scene_depth [[texture(1)]],
    sampler nearest_sampler [[sampler(0)]])
{
    float4 texel = atlas.sample(nearest_sampler, in.uv);
    if (texel.a * in.color.a < 0.5f) discard_fragment();

    if (in.depth_test > 0.5f) {
        if (uniforms.has_depth < 0.5f) discard_fragment();
        float2 output_size = max(
            float2(uniforms.output_width, uniforms.output_height),
            float2(1.0f)
        );
        float2 screen_uv = clamp(in.position.xy / output_size, float2(0.0f), float2(0.999999f));
        uint2 depth_size = uint2(scene_depth.get_width(), scene_depth.get_height());
        uint2 pixel = min(uint2(screen_uv * float2(depth_size)), depth_size - uint2(1u));
        float surface_depth = scene_depth.read(pixel).r;
        float epsilon = max(0.0025f, surface_depth * 0.0005f);
        if (in.linear_depth > surface_depth + epsilon) discard_fragment();
    }

    return float4(in.color.rgb, 1.0f);
}
)MSL";

bool createAtlas()
{
    Models::Images::Image image;
    std::string error;
    const bool loaded = Models::Images::load(FontAtlas::ASSET_PATH, &image, &error)
        && image.width == FontAtlas::WIDTH
        && image.height == FontAtlas::HEIGHT
        && image.rgba.size() == static_cast<std::size_t>(FontAtlas::WIDTH * FontAtlas::HEIGHT * 4);

    if (!loaded) {
        image.width = FontAtlas::WIDTH;
        image.height = FontAtlas::HEIGHT;
        image.rgba = FontAtlas::rgba();
        image.meaningful_alpha = true;
    }

    const LWMGLTextureDesc atlas_desc = {
        static_cast<std::uint32_t>(image.width),
        static_cast<std::uint32_t>(image.height),
        LWMGL_RGBA8_UNORM,
        LWMGL_TEXTURE_SAMPLED,
        LWMGL_STORAGE_SHARED,
    };
    state.atlas = Metal.createTexture(&atlas_desc);
    return state.atlas
        && Metal.uploadTexture2D(
            state.atlas,
            image.rgba.data(),
            static_cast<std::size_t>(image.width) * 4u
        ) == 0;
}

bool createEmptyDepth()
{
    const LWMGLTextureDesc desc = {
        1u,
        1u,
        LWMGL_RGBA32_FLOAT,
        LWMGL_TEXTURE_SAMPLED,
        LWMGL_STORAGE_SHARED,
    };
    state.empty_depth = Metal.createTexture(&desc);
    if (!state.empty_depth) return false;
    const float pixel[4] = {1.0e30f, 0.0f, 0.0f, 0.0f};
    return Metal.uploadTexture2D(state.empty_depth, pixel, sizeof pixel) == 0;
}

bool ensureState()
{
    if (state.pipeline && state.atlas && state.sampler && state.uniforms && state.empty_depth) return true;
    if (!Metal.isCreated()) return false;

    state.library = Metal.createLibraryFromSource(shader_source, std::char_traits<char>::length(shader_source));
    if (!state.library) return false;
    state.vertex_function = Metal.createFunction(state.library, "font_vertex");
    state.fragment_function = Metal.createFunction(state.library, "font_fragment");
    if (!state.vertex_function || !state.fragment_function) return false;
    state.pipeline = Metal.createRenderPipeline(
        state.vertex_function,
        state.fragment_function,
        LWMGL_BGRA8_UNORM
    );
    if (!state.pipeline) return false;

    const LWMGLSamplerDesc sampler_desc = {
        LWMGL_FILTER_NEAREST,
        LWMGL_FILTER_NEAREST,
        LWMGL_ADDRESS_CLAMP,
        LWMGL_ADDRESS_CLAMP,
    };
    state.sampler = Metal.createSampler(&sampler_desc);
    if (!state.sampler || !createAtlas() || !createEmptyDepth()) return false;

    const LWMGLBufferDesc uniform_desc = {sizeof(MetalUniforms), LWMGL_STORAGE_SHARED};
    state.uniforms = Metal.createBuffer(&uniform_desc, nullptr);
    return state.uniforms != nullptr;
}

bool ensureVertexBuffer(std::size_t bytes)
{
    if (state.vertices && state.vertex_capacity >= bytes) return true;
    if (state.vertices) Metal.destroyBuffer(state.vertices);
    state.vertices = nullptr;
    state.vertex_capacity = 0u;

    const std::size_t capacity = std::max<std::size_t>(bytes, sizeof(FontVertex) * 256u);
    const LWMGLBufferDesc desc = {capacity, LWMGL_STORAGE_SHARED};
    state.vertices = Metal.createBuffer(&desc, nullptr);
    if (!state.vertices) return false;
    state.vertex_capacity = capacity;
    return true;
}

} // namespace

void renderFontsMetal(const FontBatches& batches, FrameOutput& output)
{
    LWMGLCommand command = static_cast<LWMGLCommand>(output.command);
    if (!command || !ensureState()) return;

    const bool has_linear_depth =
        output.depth == DepthSource::LinearTexture && output.depth_texture != nullptr;

    std::vector<FontVertex> vertices;
    vertices.reserve(
        batches.overlay.size() + (has_linear_depth ? batches.depth.size() : 0u)
    );
    if (has_linear_depth) vertices.insert(vertices.end(), batches.depth.begin(), batches.depth.end());
    vertices.insert(vertices.end(), batches.overlay.begin(), batches.overlay.end());
    if (vertices.empty()) return;

    const std::size_t bytes = vertices.size() * sizeof(FontVertex);
    if (!ensureVertexBuffer(bytes)) return;
    if (Metal.uploadBuffer(state.vertices, 0u, vertices.data(), bytes) != 0) return;

    const MetalUniforms uniforms = {
        static_cast<float>(std::max(output.width, 1)),
        static_cast<float>(std::max(output.height, 1)),
        has_linear_depth ? 1.0f : 0.0f,
        0.0f,
    };
    if (Metal.uploadBuffer(state.uniforms, 0u, &uniforms, sizeof uniforms) != 0) return;

    LWMGLTexture depth = has_linear_depth
        ? static_cast<LWMGLTexture>(output.depth_texture)
        : state.empty_depth;

    if (Metal.setRenderPipeline(command, state.pipeline) != 0) return;
    if (Metal.setBuffer(command, state.vertices, 0u, 0u) != 0) return;
    if (Metal.setFragmentBuffer(command, state.uniforms, 0u, 0u) != 0) return;
    if (Metal.setFragmentTexture(command, state.atlas, 0u) != 0) return;
    if (Metal.setFragmentTexture(command, depth, 1u) != 0) return;
    if (Metal.setFragmentSampler(command, state.sampler, 0u) != 0) return;
    (void)Metal.draw(command, 0u, static_cast<std::uint32_t>(vertices.size()));
}

void shutdownFontsMetal()
{
    if (state.vertices) Metal.destroyBuffer(state.vertices);
    if (state.uniforms) Metal.destroyBuffer(state.uniforms);
    if (state.sampler) Metal.destroySampler(state.sampler);
    if (state.empty_depth) Metal.destroyTexture(state.empty_depth);
    if (state.atlas) Metal.destroyTexture(state.atlas);
    if (state.pipeline) Metal.destroyRenderPipeline(state.pipeline);
    if (state.fragment_function) Metal.destroyFunction(state.fragment_function);
    if (state.vertex_function) Metal.destroyFunction(state.vertex_function);
    if (state.library) Metal.destroyLibrary(state.library);
    state = {};
}

} // namespace Renderer::Internal

#endif
