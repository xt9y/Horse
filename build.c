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
    C_Target *library = c_shared_library(b, "Horse");

    c_sources(library, "Sources/*.cpp");
    c_sources(library, "Sources/*/*.cpp");
    c_sources(library, "Sources/Models/*/*.cpp");
    c_sources(library, "Sources/Renderer/Rasterizer/Rasterizer.cpp");
#ifdef __APPLE__
    c_sources(library, "Sources/Renderer/PathTracer/PathTracerMetal.cpp");
#else
    c_sources(library, "Sources/Renderer/PathTracer/PathTracer.cpp");
#endif

    c_flag(library, "-std=c++20");
    configureLibrary(library);

    c_default_target(b, library);
}
