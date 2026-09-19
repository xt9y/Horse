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

static void configureTest(C_Target *target, C_Target *library)
{
    c_flag(target, "-std=c++20");
    c_warnings_strict(target);
    c_include(target, ".");
    c_include(target, "Sources");
    configurePlatform(target);
#ifdef __APPLE__
    c_link_flag(target, "-Wl,-rpath,@loader_path");
#endif
    c_link_target(target, library);
}

void build(C_Build *b)
{
    C_Dependency *imgui = c_git(b, "imgui", "https://github.com/ocornut/imgui.git", "v1.92.9b");
    c_dep_source(imgui);
    c_dep_include(imgui, ".");
    c_dep_include(imgui, "backends");
    c_dep_sources(imgui, "imgui.cpp");
    c_dep_sources(imgui, "imgui_draw.cpp");
    c_dep_sources(imgui, "imgui_tables.cpp");
    c_dep_sources(imgui, "imgui_widgets.cpp");
    c_dep_sources(imgui, "backends/imgui_impl_sdl3.cpp");
    c_dep_sources(imgui, "backends/imgui_impl_sdlgpu3.cpp");
#ifndef _WIN32
    c_dep_flag(imgui, "-fPIC");
#endif
#ifdef __APPLE__
    c_dep_flag(imgui, "-I/opt/homebrew/include");
#endif
    c_dep_flag(imgui, "-I/usr/local/include");

    C_Target *library = c_shared_library(b, "Horse");
    c_sources(library, "Sources/*/*.cpp");
    c_sources(library, "Sources/Core/*/*.cpp");
    c_sources(library, "Sources/Models/*/*.cpp");
    c_sources(library, "Sources/Renderer/*/*.cpp");
    c_sources(library, "Sources/Renderer/GlobalIllumination/PhotonMapping/*.cpp");
    c_flag(library, "-std=c++20");
    configureLibrary(library);
    c_use(library, imgui);

    C_Target *model_loading_tests = c_test(b, "HorseModelLoadingTests");
    c_sources(model_loading_tests, "Tests/ModelLoading.cpp");
    configureTest(model_loading_tests, library);

    C_Target *model_cache_tests = c_test(b, "HorseModelCacheValidationTests");
    c_sources(model_cache_tests, "Tests/ModelCacheValidation.cpp");
    configureTest(model_cache_tests, library);

    C_Target *model_cache_binary_tests = c_test(b, "HorseModelCacheBinaryTests");
    c_sources(model_cache_binary_tests, "Tests/ModelCacheBinary.cpp");
    configureTest(model_cache_binary_tests, library);

    C_Target *gltf_material_dependency_tests = c_test(b, "HorseGltfMaterialDependencyTests");
    c_sources(gltf_material_dependency_tests, "Tests/GltfMaterialDependencies.cpp");
    configureTest(gltf_material_dependency_tests, library);

    C_Target *gltf_texture_derivation_tests = c_test(b, "HorseGltfTextureDerivationTests");
    c_sources(gltf_texture_derivation_tests, "Tests/GltfTextureDerivation.cpp");
    configureTest(gltf_texture_derivation_tests, library);

    C_Target *texture_cache_clear_tests = c_test(b, "HorseTextureCacheClearTests");
    c_sources(texture_cache_clear_tests, "Tests/TextureCacheClear.cpp");
    configureTest(texture_cache_clear_tests, library);

    C_Target *streaming_failure_tests = c_test(b, "HorseTextureStreamingFailureTests");
    c_sources(streaming_failure_tests, "Tests/TextureStreamingFailure.cpp");
    configureTest(streaming_failure_tests, library);

    C_Target *streaming_tests = c_test(b, "HorseTextureStreamingBackpressureTests");
    c_sources(streaming_tests, "Tests/TextureStreamingBackpressure.cpp");
    configureTest(streaming_tests, library);

    C_Target *model_scene_tests = c_test(b, "HorseModelScenePendingTests");
    c_sources(model_scene_tests, "Tests/ModelScenePending.cpp");
    configureTest(model_scene_tests, library);

    C_Target *job_shutdown_tests = c_test(b, "HorseJobShutdownTests");
    c_sources(job_shutdown_tests, "Tests/JobsShutdown.cpp");
    configureTest(job_shutdown_tests, library);

    C_Target *job_group_tests = c_test(b, "HorseJobGroupTests");
    c_sources(job_group_tests, "Tests/JobGroups.cpp");
    configureTest(job_group_tests, library);

    C_Target *raster_topology_tests = c_test(b, "HorseRasterTopologyRevisionTests");
    c_sources(raster_topology_tests, "Tests/RasterTopologyRevision.cpp");
    configureTest(raster_topology_tests, library);

    C_Target *raster_submission_tests = c_test(b, "HorseRasterIndexedSubmissionTests");
    c_sources(raster_submission_tests, "Tests/RasterIndexedSubmission.cpp");
    configureTest(raster_submission_tests, library);

    C_Target *staged_model_tests = c_test(b, "HorseStagedModelTests");
    c_sources(staged_model_tests, "Tests/StagedModel.cpp");
    configureTest(staged_model_tests, library);

    C_Target *async_model_tests = c_test(b, "HorseAsyncModelLoadingTests");
    c_sources(async_model_tests, "Tests/AsyncModelLoading.cpp");
    configureTest(async_model_tests, library);

    C_Target *async_model_lifetime_tests = c_test(b, "HorseAsyncModelLoadingLifetimeTests");
    c_sources(async_model_lifetime_tests, "Tests/AsyncModelLoadingLifetime.cpp");
    configureTest(async_model_lifetime_tests, library);

    C_Target *async_gltf_tests = c_test(b, "HorseAsyncGltfLoadingTests");
    c_sources(async_gltf_tests, "Tests/AsyncGltfLoading.cpp");
    configureTest(async_gltf_tests, library);

    C_Target *async_model_cache_tests = c_test(b, "HorseAsyncModelCacheTests");
    c_sources(async_model_cache_tests, "Tests/AsyncModelCache.cpp");
    configureTest(async_model_cache_tests, library);

    C_Target *automatic_resource_pump_tests = c_test(b, "HorseAutomaticResourcePumpTests");
    c_sources(automatic_resource_pump_tests, "Tests/AutomaticResourcePump.cpp");
    configureTest(automatic_resource_pump_tests, library);

    C_Target *renderer_settings_tests = c_test(b, "HorseRendererSettingsTests");
    c_sources(renderer_settings_tests, "Tests/RendererSettings.cpp");
    configureTest(renderer_settings_tests, library);

    C_Target *hiz_tests = c_test(b, "HorseHiZTests");
    c_sources(hiz_tests, "Tests/HiZ.cpp");
    configureTest(hiz_tests, library);

    C_Target *forward_plus_tests = c_test(b, "HorseForwardPlusTests");
    c_sources(forward_plus_tests, "Tests/ForwardPlus.cpp");
    configureTest(forward_plus_tests, library);

    C_Target *shadow_cascade_tests = c_test(b, "HorseShadowCascadeTests");
    c_sources(shadow_cascade_tests, "Tests/ShadowCascades.cpp");
    configureTest(shadow_cascade_tests, library);

    C_Target *shadow_cache_tests = c_test(b, "HorseShadowCacheTests");
    c_sources(shadow_cache_tests, "Tests/ShadowCache.cpp");
    configureTest(shadow_cache_tests, library);

    C_Target *ambient_occlusion_tests = c_test(b, "HorseAmbientOcclusionTests");
    c_sources(ambient_occlusion_tests, "Tests/AmbientOcclusion.cpp");
    configureTest(ambient_occlusion_tests, library);

    C_Target *reflection_tests = c_test(b, "HorseReflectionTests");
    c_sources(reflection_tests, "Tests/Reflections.cpp");
    configureTest(reflection_tests, library);

    C_Target *reflection_probe_tests = c_test(b, "HorseReflectionProbeTests");
    c_sources(reflection_probe_tests, "Tests/ReflectionProbes.cpp");
    configureTest(reflection_probe_tests, library);

    C_Target *reflection_shader_tests = c_test(b, "HorseReflectionShaderLayoutTests");
    c_sources(reflection_shader_tests, "Tests/ReflectionShaderLayout.cpp");
    configureTest(reflection_shader_tests, library);

    C_Target *reflection_prefilter_tests = c_test(b, "HorseReflectionPrefilterTests");
    c_sources(reflection_prefilter_tests, "Tests/ReflectionPrefilter.cpp");
    configureTest(reflection_prefilter_tests, library);

    C_Target *volumetrics_layout_tests = c_test(b, "HorseVolumetricsComputeLayoutTests");
    c_sources(volumetrics_layout_tests, "Tests/VolumetricsComputeLayout.cpp");
    configureTest(volumetrics_layout_tests, library);

    C_Target *render_graph_tests = c_test(b, "HorseRenderGraphTests");
    c_sources(render_graph_tests, "Tests/RenderGraph.cpp");
    configureTest(render_graph_tests, library);

    C_Target *raster_render_graph_tests = c_test(b, "HorseRasterRenderGraphTests");
    c_sources(raster_render_graph_tests, "Tests/RasterRenderGraph.cpp");
    configureTest(raster_render_graph_tests, library);

    C_Target *scene_cache_profiling_tests = c_test(b, "HorseSceneCacheProfilingTests");
    c_sources(scene_cache_profiling_tests, "Tests/SceneCacheProfiling.cpp");
    configureTest(scene_cache_profiling_tests, library);

    c_default_target(b, library);
}
