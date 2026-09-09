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

    C_Target *pathtracer_depth_contract = c_test(b, "pathtracer-depth-contract");
    c_sources(pathtracer_depth_contract, "tests/pathtracer_depth_contract.cpp");
    configureContract(pathtracer_depth_contract);

    C_Target *metal_frame_contract = c_test(b, "metal-frame-contract");
    c_sources(metal_frame_contract, "tests/metal_frame_contract.cpp");
    configureContract(metal_frame_contract);

    C_Target *camera_view_contract = c_test(b, "camera-view-contract");
    c_sources(camera_view_contract, "tests/camera_view_contract.cpp");
    c_sources(camera_view_contract, "Sources/Renderer/Math.cpp");
    configureContract(camera_view_contract);

    C_Target *camera_mouse_grab_contract = c_test(b, "camera-mouse-grab-contract");
    c_sources(camera_mouse_grab_contract, "tests/camera_mouse_grab_contract.cpp");
    configureContract(camera_mouse_grab_contract);

    C_Target *opengl_present_contract = c_test(b, "opengl-present-contract");
    c_sources(opengl_present_contract, "tests/opengl_present_contract.cpp");
    configureContract(opengl_present_contract);

    C_Target *font_opengl_uv_contract = c_test(b, "font-opengl-uv-contract");
    c_sources(font_opengl_uv_contract, "tests/font_opengl_uv_contract.cpp");
    configureContract(font_opengl_uv_contract);

    C_Target *global_illumination_contract = c_test(b, "global-illumination-contract");
    c_sources(global_illumination_contract, "tests/global_illumination_contract.cpp");
    configureContract(global_illumination_contract);

    C_Target *pathtracer_alpha_cutout_contract = c_test(b, "pathtracer-alpha-cutout-contract");
    c_sources(pathtracer_alpha_cutout_contract, "tests/pathtracer_alpha_cutout_contract.cpp");
    configureContract(pathtracer_alpha_cutout_contract);

    C_Target *rasterizer_lighting_contract = c_test(b, "rasterizer-lighting-contract");
    c_sources(rasterizer_lighting_contract, "tests/rasterizer_lighting_contract.cpp");
    configureContract(rasterizer_lighting_contract);

    C_Target *material_opacity_mask_contract = c_test(b, "material-opacity-mask-contract");
    c_sources(material_opacity_mask_contract, "tests/material_opacity_mask_contract.cpp");
    configureContract(material_opacity_mask_contract);

    C_Target *rasterizer_output_contract = c_test(b, "rasterizer-output-contract");
    c_sources(rasterizer_output_contract, "tests/rasterizer_output_contract.cpp");
    configureContract(rasterizer_output_contract);

    C_Target *pathtracer_alpha_slot_contract = c_test(b, "pathtracer-alpha-slot-contract");
    c_sources(pathtracer_alpha_slot_contract, "tests/pathtracer_alpha_slot_contract.cpp");
    configureContract(pathtracer_alpha_slot_contract);

    C_Target *rasterizer_material_stability_contract = c_test(b, "rasterizer-material-stability-contract");
    c_sources(rasterizer_material_stability_contract, "tests/rasterizer_material_stability_contract.cpp");
    configureContract(rasterizer_material_stability_contract);

    C_Target *pathtracer_motion_reconstruction_contract = c_test(b, "pathtracer-motion-reconstruction-contract");
    c_sources(pathtracer_motion_reconstruction_contract, "tests/pathtracer_motion_reconstruction_contract.cpp");
    configureContract(pathtracer_motion_reconstruction_contract);

    C_Target *pathtracer_texture_residency_contract = c_test(b, "pathtracer-texture-residency-contract");
    c_sources(pathtracer_texture_residency_contract, "tests/pathtracer_texture_residency_contract.cpp");
    configureContract(pathtracer_texture_residency_contract);

    C_Target *rasterizer_sampler_fallback_contract = c_test(b, "rasterizer-sampler-fallback-contract");
    c_sources(rasterizer_sampler_fallback_contract, "tests/rasterizer_sampler_fallback_contract.cpp");
    configureContract(rasterizer_sampler_fallback_contract);

    C_Target *rasterizer_shadow_quality_contract = c_test(b, "rasterizer-shadow-quality-contract");
    c_sources(rasterizer_shadow_quality_contract, "tests/rasterizer_shadow_quality_contract.cpp");
    configureContract(rasterizer_shadow_quality_contract);

    c_default_target(b, library);
}
