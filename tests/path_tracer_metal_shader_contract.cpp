#ifdef __APPLE__
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <lwmgl/lwmgl.h>

#include "Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

struct alignas(16) TraceUniforms {
    std::array<float, 4> camera_position{};
    std::array<float, 4> camera_forward{0.0f, 0.0f, -1.0f, 0.0f};
    std::array<float, 4> camera_right{1.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> camera_up{0.0f, 1.0f, 0.0f, 0.0f};
    std::array<float, 4> light_position_intensity{};
    std::array<float, 4> light_color_tan_half_fov{1.0f, 1.0f, 1.0f, 0.577350269f};
    std::array<float, 4> resolution_aspect{1.0f, 1.0f, 1.0f, 0.0f};
    std::array<std::int32_t, 4> counts{0, 0, 1, 1};
    std::array<std::uint32_t, 4> frame{0u, 1u, 0u, 0u};
};

struct alignas(16) PresentUniforms {
    std::array<float, 4> exposure{1.0f, 0.0f, 0.0f, 0.0f};
};

int fail(const char *stage, int code)
{
    std::fprintf(stderr, "%s: %s\n", stage, lwmglGetLastError() ? lwmglGetLastError() : "unknown error");
    return code;
}

} // namespace

int main()
{
    static_assert(sizeof(TraceUniforms) == 144u);
    static_assert(sizeof(PresentUniforms) == 16u);

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(64, 64, "horse-metal-shader-contract", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 2; }
    if (Metal.create(window) != 0) return fail("Metal.create", 3);

    LWMGLLibrary library = Metal.createLibraryFromSource(
        Renderer::PathTracerMetalShaders::source,
        std::strlen(Renderer::PathTracerMetalShaders::source)
    );
    if (!library) return fail("library", 4);

    LWMGLFunction trace = Metal.createFunction(library, "trace_kernel");
    if (!trace) return fail("trace_kernel", 5);
    LWMGLFunction vertex = Metal.createFunction(library, "present_vertex");
    if (!vertex) return fail("present_vertex", 6);
    LWMGLFunction fragment = Metal.createFunction(library, "present_fragment");
    if (!fragment) return fail("present_fragment", 7);

    LWMGLComputePipeline compute = Metal.createComputePipeline(trace);
    if (!compute) return fail("compute pipeline", 8);
    LWMGLRenderPipeline present = Metal.createRenderPipeline(vertex, fragment, LWMGL_BGRA8_UNORM);
    if (!present) return fail("render pipeline", 9);

    const std::array<std::uint8_t, 48> node_zero{};
    const std::array<std::uint8_t, 128> triangle_zero{};
    const std::array<std::uint8_t, 32> material_zero{};
    const TraceUniforms trace_uniforms{};
    const PresentUniforms present_uniforms{};

    const LWMGLBufferDesc node_desc{node_zero.size(), LWMGL_STORAGE_SHARED};
    const LWMGLBufferDesc triangle_desc{triangle_zero.size(), LWMGL_STORAGE_SHARED};
    const LWMGLBufferDesc material_desc{material_zero.size(), LWMGL_STORAGE_SHARED};
    const LWMGLBufferDesc trace_uniform_desc{sizeof(trace_uniforms), LWMGL_STORAGE_SHARED};
    const LWMGLBufferDesc present_uniform_desc{sizeof(present_uniforms), LWMGL_STORAGE_SHARED};

    LWMGLBuffer node_buffer = Metal.createBuffer(&node_desc, node_zero.data());
    LWMGLBuffer triangle_buffer = Metal.createBuffer(&triangle_desc, triangle_zero.data());
    LWMGLBuffer material_buffer = Metal.createBuffer(&material_desc, material_zero.data());
    LWMGLBuffer trace_uniform_buffer = Metal.createBuffer(&trace_uniform_desc, &trace_uniforms);
    LWMGLBuffer present_uniform_buffer = Metal.createBuffer(&present_uniform_desc, &present_uniforms);
    if (!node_buffer || !triangle_buffer || !material_buffer || !trace_uniform_buffer || !present_uniform_buffer) {
        return fail("buffers", 10);
    }

    const LWMGLTextureDesc accumulation_desc{
        1u, 1u, LWMGL_RGBA32_FLOAT,
        LWMGL_TEXTURE_SAMPLED | LWMGL_TEXTURE_READ | LWMGL_TEXTURE_WRITE,
        LWMGL_STORAGE_PRIVATE
    };
    const LWMGLTextureDesc white_desc{
        1u, 1u, LWMGL_RGBA8_UNORM,
        LWMGL_TEXTURE_SAMPLED,
        LWMGL_STORAGE_SHARED
    };
    LWMGLTexture accumulation = Metal.createTexture(&accumulation_desc);
    LWMGLTexture white = Metal.createTexture(&white_desc);
    if (!accumulation || !white) return fail("textures", 11);
    const std::uint8_t white_pixel[4] = {255u, 255u, 255u, 255u};
    if (Metal.uploadTexture2D(white, white_pixel, 4u) != 0) return fail("white upload", 12);

    const LWMGLSamplerDesc sampler_desc{
        LWMGL_FILTER_LINEAR,
        LWMGL_FILTER_LINEAR,
        LWMGL_ADDRESS_CLAMP,
        LWMGL_ADDRESS_CLAMP
    };
    LWMGLSampler sampler = Metal.createSampler(&sampler_desc);
    if (!sampler) return fail("sampler", 13);

    LWMGLCommand command = Metal.begin();
    if (!command) return fail("command", 14);
    if (Metal.beginCompute(command) != 0) return fail("begin compute", 15);
    if (Metal.setComputePipeline(command, compute) != 0) return fail("set compute pipeline", 16);
    if (Metal.setBuffer(command, node_buffer, 0u, 0u) != 0) return fail("node binding", 17);
    if (Metal.setBuffer(command, triangle_buffer, 0u, 1u) != 0) return fail("triangle binding", 18);
    if (Metal.setBuffer(command, material_buffer, 0u, 2u) != 0) return fail("material binding", 19);
    if (Metal.setBuffer(command, trace_uniform_buffer, 0u, 3u) != 0) return fail("trace uniform binding", 20);
    if (Metal.setTexture(command, accumulation, 0u) != 0) return fail("accumulation binding", 21);
    for (std::uint32_t slot = 0u; slot < 16u; ++slot) {
        if (Metal.setTexture(command, white, slot + 1u) != 0) return fail("material texture binding", 22);
    }
    if (Metal.setSampler(command, sampler, 0u) != 0) return fail("compute sampler", 23);
    if (Metal.dispatch(command, 1u, 1u, 1u) != 0) return fail("dispatch", 24);
    if (Metal.endEncoding(command) != 0) return fail("end compute", 25);

    const LWMGLClearColor clear{0.0, 0.0, 0.0, 1.0};
    if (Metal.beginRenderToDrawable(command, clear, 1) != 0) return fail("begin render", 26);
    if (Metal.setRenderPipeline(command, present) != 0) return fail("set render pipeline", 27);
    if (Metal.setFragmentBuffer(command, present_uniform_buffer, 0u, 0u) != 0) return fail("present uniform binding", 28);
    if (Metal.setFragmentTexture(command, accumulation, 0u) != 0) return fail("present texture binding", 29);
    if (Metal.setFragmentSampler(command, sampler, 0u) != 0) return fail("present sampler binding", 30);
    if (Metal.draw(command, 0u, 3u) != 0) return fail("draw", 31);
    if (Metal.present(command) != 0) return fail("present", 32);
    if (Metal.commit(command) != 0) return fail("commit", 33);
    if (Metal.wait(command) != 0) return fail("wait", 34);

    Metal.destroyCommand(command);
    Metal.destroySampler(sampler);
    Metal.destroyTexture(white);
    Metal.destroyTexture(accumulation);
    Metal.destroyBuffer(present_uniform_buffer);
    Metal.destroyBuffer(trace_uniform_buffer);
    Metal.destroyBuffer(material_buffer);
    Metal.destroyBuffer(triangle_buffer);
    Metal.destroyBuffer(node_buffer);
    Metal.destroyComputePipeline(compute);
    Metal.destroyRenderPipeline(present);
    Metal.destroyFunction(fragment);
    Metal.destroyFunction(vertex);
    Metal.destroyFunction(trace);
    Metal.destroyLibrary(library);
    Metal.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
#else
int main() { return 0; }
#endif
