#include <cbuild.h>

static void configurePlatform(C_Target *target)
{
#ifdef __APPLE__
    c_define(target, "GL_SILENCE_DEPRECATION");
    c_include(target, "/opt/homebrew/include");
    c_include(target, "/usr/local/include/lwmgl-1.0.0");
    c_link_flag(target, "-L/opt/homebrew/lib");
    c_framework(target, "OpenGL");
    c_framework(target, "Cocoa");
    c_framework(target, "IOKit");
    c_framework(target, "CoreVideo");
    c_framework(target, "Metal");
    c_framework(target, "QuartzCore");
    c_link_system(target, "c++");
#else
    c_link_system(target, "GL");
    c_link_system(target, "GLU");
    c_link_system(target, "m");
    c_link_system(target, "dl");
    c_link_system(target, "pthread");
    c_link_system(target, "stdc++");
#endif
    c_link_system(target, "glfw");
}

static void configureLibrary(C_Target *target)
{
    c_warnings_strict(target);
    c_include(target, ".");
    c_include(target, "Sources");
    c_include(target, "/usr/local/include/lwcgl-2.9.3");

    configurePlatform(target);

    c_link_flag(target, "-L/usr/local/lib");
    c_link_flag(target, "-llwcgl");
#ifdef __APPLE__
    c_link_flag(target, "-llwmgl");
#endif
    c_link_flag(target, "-Wl,-rpath,/usr/local/lib");
#ifdef __APPLE__
    c_link_flag(target, "-Wl,-install_name,@rpath/libHorse.dylib");
#else
    c_link_flag(target, "-Wl,-soname,libHorse.so");
#endif
}

static void configureContract(C_Target *target)
{
    c_include(target, ".");
    c_include(target, "Sources");
    c_flag(target, "-std=c++20");
    c_warnings_strict(target);
#ifdef __APPLE__
    c_link_system(target, "c++");
#else
    c_link_system(target, "stdc++");
#endif
}

void build(C_Build *b)
{
    C_Dependency *imgui = c_git(
        b,
        "imgui",
        "https://github.com/ocornut/imgui.git",
        "v1.92.9b"
    );
    c_dep_source(imgui);
    c_dep_include(imgui, ".");
    c_dep_include(imgui, "backends");
    c_dep_sources(imgui, "imgui.cpp");
    c_dep_sources(imgui, "imgui_draw.cpp");
    c_dep_sources(imgui, "imgui_tables.cpp");
    c_dep_sources(imgui, "imgui_widgets.cpp");
    c_dep_sources(imgui, "backends/imgui_impl_glfw.cpp");
#ifdef __APPLE__
    c_dep_sources(imgui, "backends/imgui_impl_opengl2.cpp");
    c_dep_sources(imgui, "backends/imgui_impl_metal.mm");
    c_dep_flag(imgui, "-I/opt/homebrew/include");
    c_dep_flag(imgui, "-DGL_SILENCE_DEPRECATION");
#else
    c_dep_sources(imgui, "backends/imgui_impl_opengl3.cpp");
#endif

    C_Target *library = c_shared_library(b, "Horse");

    c_sources(library, "Sources/*.cpp");
    c_sources(library, "Sources/*/*.cpp");
    c_sources(library, "Sources/Models/*/*.cpp");

    c_sources(library, "Sources/Renderer/Fonts/FontAtlas.cpp");
    c_sources(library, "Sources/Renderer/Fonts/FontLayout.cpp");
    c_sources(library, "Sources/Renderer/Fonts/FontPass.cpp");
    c_sources(library, "Sources/Renderer/Fonts/OpenGL/FontPassOpenGL.cpp");

    c_sources(library, "Sources/Renderer/GlobalIllumination/GlobalIllumination.cpp");
    c_sources(library, "Sources/Renderer/GlobalIllumination/TraceScene.cpp");
    c_sources(library, "Sources/Renderer/GlobalIllumination/PhotonMapping/PhotonMap.cpp");

    c_sources(library, "Sources/Renderer/Scenes/Scene.cpp");
    c_sources(library, "Sources/Renderer/Scenes/SceneCache.cpp");
    c_sources(library, "Sources/Renderer/Systems/OpenGL/Program.cpp");
    c_sources(library, "Sources/Renderer/Systems/OpenGL/TextureCache.cpp");
    c_sources(library, "Sources/Renderer/Rasterizer/OpenGL/RasterizerOpenGL.cpp");
    c_sources(library, "Sources/UI/OpenGL/UIOpenGL.cpp");

#ifdef __APPLE__
    c_sources(library, "Sources/Renderer/Fonts/Metal/FontPassMetal.cpp");
    c_sources(library, "Sources/Renderer/GlobalIllumination/Metal/GlobalIlluminationMetal.cpp");
    c_sources(library, "Sources/Renderer/Scenes/Metal/SceneResources.cpp");
    c_sources(library, "Sources/Renderer/Systems/Metal/Uniforms.cpp");
    c_sources(library, "Sources/Renderer/PathTracer/Metal/PathTracerMetal.cpp");
    c_sources(library, "Sources/Renderer/RayTracer/Metal/RayTracerMetal.cpp");
    c_sources(library, "Sources/UI/Metal/UIMetal.mm");
#else
    c_sources(library, "Sources/Renderer/GlobalIllumination/OpenGL/GlobalIlluminationOpenGL.cpp");
    c_sources(library, "Sources/Renderer/Scenes/OpenGL/SceneResources.cpp");
    c_sources(library, "Sources/Renderer/PathTracer/OpenGL/PathTracerOpenGL.cpp");
    c_sources(library, "Sources/Renderer/RayTracer/OpenGL/RayTracerOpenGL.cpp");
#endif

    c_flag(library, "-std=c++20");
    configureLibrary(library);
    c_use(library, imgui);

    C_Target *renderer_systems_contract = c_test(b, "renderer-systems-contract");
    c_sources(renderer_systems_contract, "tests/renderer_systems_contract.cpp");
    configureContract(renderer_systems_contract);

    C_Target *photon_mapping_contract = c_test(b, "photon-mapping-contract");
    c_sources(photon_mapping_contract, "tests/photon_mapping_contract.cpp");
    configureContract(photon_mapping_contract);

    C_Target *metal_namespace_contract = c_test(b, "metal-namespace-contract");
    c_sources(metal_namespace_contract, "tests/metal_namespace_contract.cpp");
    configureContract(metal_namespace_contract);

    C_Target *imgui_contract = c_test(b, "imgui-contract");
    c_sources(imgui_contract, "tests/imgui_contract.cpp");
    configureContract(imgui_contract);

    c_default_target(b, library);
}
