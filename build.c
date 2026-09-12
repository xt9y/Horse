#include <cbuild.h>

static void configurePlatform(C_Target *target)
{
#ifdef __APPLE__
    c_include(target, "/opt/homebrew/include");
    c_link_flag(target, "-L/opt/homebrew/lib");
#endif
    c_include(target, "/usr/local/include/SDL3_shadercross");
    c_link_flag(target, "-L/usr/local/lib");
    c_link_flag(target, "-lSDL3_shadercross");
    c_link_flag(target, "-lSDL3");
#ifdef __APPLE__
    c_link_flag(target, "-Wl,-rpath,/opt/homebrew/lib");
    c_link_flag(target, "-Wl,-rpath,/usr/local/lib");
    c_link_system(target, "c++");
#else
    c_link_system(target, "m");
    c_link_system(target, "dl");
    c_link_system(target, "pthread");
    c_link_system(target, "stdc++");
#endif
}

static void configureLibrary(C_Target *target)
{
    c_warnings_strict(target);
    c_include(target, ".");
    c_include(target, "Sources");

    configurePlatform(target);

#ifdef __APPLE__
    c_link_flag(target, "-Wl,-install_name,@rpath/libHorse.dylib");
#else
    c_link_flag(target, "-Wl,-rpath,/usr/local/lib");
    c_link_flag(target, "-Wl,-soname,libHorse.so");
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
    c_dep_sources(imgui, "backends/imgui_impl_sdl3.cpp");
    c_dep_sources(imgui, "backends/imgui_impl_sdlgpu3.cpp");
#ifdef __APPLE__
    c_dep_flag(imgui, "-I/opt/homebrew/include");
#endif
    c_dep_flag(imgui, "-I/usr/local/include");

    C_Target *library = c_shared_library(b, "Horse");

    c_sources(library, "Sources/*.cpp");
    c_sources(library, "Sources/*/*.cpp");
    c_sources(library, "Sources/Models/*/*.cpp");
    c_sources(library, "Sources/Renderer/*/*.cpp");
    c_sources(library, "Sources/Renderer/GlobalIllumination/PhotonMapping/*.cpp");

    c_flag(library, "-std=c++20");
    configureLibrary(library);
    c_use(library, imgui);

    c_default_target(b, library);
}
