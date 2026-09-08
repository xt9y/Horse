#ifdef __APPLE__
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <lwmgl/lwmgl.h>

#include "Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <cstdio>
#include <cstring>

int main()
{
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(64, 64, "horse-metal-shader-contract", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 2; }
    if (Metal.create(window) != 0) {
        std::fprintf(stderr, "Metal.create: %s\n", lwmglGetLastError());
        return 3;
    }

    LWMGLLibrary library = Metal.createLibraryFromSource(
        Renderer::PathTracerMetalShaders::source,
        std::strlen(Renderer::PathTracerMetalShaders::source)
    );
    if (!library) {
        std::fprintf(stderr, "library: %s\n", lwmglGetLastError());
        return 4;
    }

    LWMGLFunction trace = Metal.createFunction(library, "trace_kernel");
    if (!trace) {
        std::fprintf(stderr, "trace_kernel: %s\n", lwmglGetLastError());
        return 5;
    }
    LWMGLFunction vertex = Metal.createFunction(library, "present_vertex");
    if (!vertex) {
        std::fprintf(stderr, "present_vertex: %s\n", lwmglGetLastError());
        return 6;
    }
    LWMGLFunction fragment = Metal.createFunction(library, "present_fragment");
    if (!fragment) {
        std::fprintf(stderr, "present_fragment: %s\n", lwmglGetLastError());
        return 7;
    }

    LWMGLComputePipeline compute = Metal.createComputePipeline(trace);
    if (!compute) {
        std::fprintf(stderr, "compute pipeline: %s\n", lwmglGetLastError());
        return 8;
    }
    LWMGLRenderPipeline present = Metal.createRenderPipeline(vertex, fragment, LWMGL_BGRA8_UNORM);
    if (!present) {
        std::fprintf(stderr, "render pipeline: %s\n", lwmglGetLastError());
        return 9;
    }

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
