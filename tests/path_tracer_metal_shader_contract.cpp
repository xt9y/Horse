#ifdef __APPLE__
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <lwmgl/lwmgl.h>

#include "Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp"

#include <cstring>

int main()
{
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(64, 64, "horse-metal-shader-contract", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 2; }
    if (Metal.create(window) != 0) return 3;

    LWMGLLibrary library = Metal.createLibraryFromSource(
        Renderer::PathTracerMetalShaders::source,
        std::strlen(Renderer::PathTracerMetalShaders::source)
    );
    if (!library) return 4;

    LWMGLFunction trace = Metal.createFunction(library, "trace_kernel");
    LWMGLFunction vertex = Metal.createFunction(library, "present_vertex");
    LWMGLFunction fragment = Metal.createFunction(library, "present_fragment");
    if (!trace || !vertex || !fragment) return 5;

    LWMGLComputePipeline compute = Metal.createComputePipeline(trace);
    LWMGLRenderPipeline present = Metal.createRenderPipeline(vertex, fragment, LWMGL_BGRA8_UNORM);
    if (!compute || !present) return 6;

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
