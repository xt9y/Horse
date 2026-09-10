#ifndef __APPLE__

#include "Renderer/PathTracer/PathTracer.hpp"

#include "Renderer/FontPass.hpp"
#include "Renderer/GlobalIllumination.hpp"
#include "Renderer/GlobalIlluminationOpenGL.hpp"
#include "Renderer/PathTracer/PathTracerShaders.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"
#include "Renderer/Systems/OpenGLSceneResources.hpp"
#include "Renderer/Systems/ProgressiveState.hpp"
#include "Renderer/Systems/Scene.hpp"
#include "Renderer/Systems/SceneCache.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t globalIlluminationSignature(const GlobalIllumination::Field *field)
{
    if (!field || !field->valid()) return 0u;
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, static_cast<std::uint32_t>(field->revision));
    hashValue(hash, static_cast<std::uint32_t>(field->revision >> 32u));
    hashFloat(hash, field->intensity);
    return hash;
}

void setInt(GLint location, int value)
{
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(GLint location, float value)
{
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setVec2(GLint location, float x, float y)
{
    if (location >= 0) GL20.glUniform2f(location, x, y);
}

void setVec3(GLint location, const Vec3& value)
{
    if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z);
}

} // namespace

struct PathTracer::Impl {
    struct TraceUniforms {
        GLint resolution = -1;
        GLint camera_position = -1;
        GLint camera_forward = -1;
        GLint camera_right = -1;
        GLint camera_up = -1;
        GLint tan_half_fov = -1;
        GLint aspect = -1;
        GLint node_count = -1;
        GLint triangle_count = -1;
        GLint material_count = -1;
        GLint samples_this_frame = -1;
        GLint sample_base = -1;
        GLint frame_index = -1;
        GLint reset_accumulation = -1;
        GLint camera_moving = -1;
        GLint stationary_phase_grid = -1;
        GLint reset_phase_grid = -1;
        GLint moving_phase_grid = -1;
        GLint moving_depth_block = -1;
        GLint has_light = -1;
        GLint light_position = -1;
        GLint light_color = -1;
        GLint light_intensity = -1;
        GLint alpha_cutoff = -1;
        std::array<GLint, Systems::OpenGLSceneResources::MaximumTextureSlots> textures{};
    };

    struct PresentUniforms {
        GLint accumulation = -1;
        GLint camera_moving = -1;
        GLint moving_phase_grid = -1;
        GLint frame_index = -1;
        GLint exposure = -1;
    };

    PathTracerSettings settings{};
    bool initialized = false;
    int width = 1;
    int height = 1;
    int trace_width = 1;
    int trace_height = 1;
    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t scene_signature = 0u;

    Systems::SceneCache scene;
    Systems::OpenGLSceneResources resources;
    Systems::ProgressiveState progressive;
    std::vector<Systems::Scene::RenderItem> render_items;

    Systems::OpenGL::Program trace_program;
    Systems::OpenGL::Program present_program;
    GLuint accumulation = 0u;
    GLuint primary_depth = 0u;
    TraceUniforms trace_uniforms{};
    PresentUniforms present_uniforms{};

    bool active() const
    {
        return initialized && settings.enabled;
    }

    bool configured() const
    {
        return settings.resolution_divisor > 0 &&
            settings.samples_per_frame > 0 &&
            settings.stationary_phase_grid > 0 &&
            settings.reset_phase_grid > 0 &&
            settings.moving_phase_grid > 0 &&
            settings.moving_depth_block > 0;
    }

    void updateTraceResolution()
    {
        const int divisor = settings.resolution_divisor;
        if (divisor <= 0) {
            trace_width = 0;
            trace_height = 0;
            return;
        }
        trace_width = std::max(width / divisor, 1);
        trace_height = std::max(height / divisor, 1);
    }

    bool createFloatTexture(GLuint& target, GLint filter)
    {
        if (trace_width <= 0 || trace_height <= 0) return false;
        GLuint texture = 0u;
        glGenTextures(1, &texture);
        if (texture == 0u) return false;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA32F,
            trace_width,
            trace_height,
            0,
            GL_RGBA,
            GL_FLOAT,
            nullptr
        );
        target = texture;
        return true;
    }

    bool createTraceTargets()
    {
        if (!createFloatTexture(accumulation, GL_LINEAR)) return false;
        if (!createFloatTexture(primary_depth, GL_NEAREST)) {
            glDeleteTextures(1, &accumulation);
            accumulation = 0u;
            return false;
        }
        progressive.reset();
        return true;
    }

    void destroyTraceTargets()
    {
        if (accumulation != 0u) glDeleteTextures(1, &accumulation);
        if (primary_depth != 0u) glDeleteTextures(1, &primary_depth);
        accumulation = 0u;
        primary_depth = 0u;
        progressive.reset();
    }

    void cacheUniforms()
    {
        trace_uniforms.resolution = trace_program.uniform("uResolution");
        trace_uniforms.camera_position = trace_program.uniform("uCameraPosition");
        trace_uniforms.camera_forward = trace_program.uniform("uCameraForward");
        trace_uniforms.camera_right = trace_program.uniform("uCameraRight");
        trace_uniforms.camera_up = trace_program.uniform("uCameraUp");
        trace_uniforms.tan_half_fov = trace_program.uniform("uTanHalfFov");
        trace_uniforms.aspect = trace_program.uniform("uAspect");
        trace_uniforms.node_count = trace_program.uniform("uNodeCount");
        trace_uniforms.triangle_count = trace_program.uniform("uTriangleCount");
        trace_uniforms.material_count = trace_program.uniform("uMaterialCount");
        trace_uniforms.samples_this_frame = trace_program.uniform("uSamplesThisFrame");
        trace_uniforms.sample_base = trace_program.uniform("uSampleBase");
        trace_uniforms.frame_index = trace_program.uniform("uFrameIndex");
        trace_uniforms.reset_accumulation = trace_program.uniform("uResetAccumulation");
        trace_uniforms.camera_moving = trace_program.uniform("uCameraMoving");
        trace_uniforms.stationary_phase_grid = trace_program.uniform("uStationaryPhaseGrid");
        trace_uniforms.reset_phase_grid = trace_program.uniform("uResetPhaseGrid");
        trace_uniforms.moving_phase_grid = trace_program.uniform("uMovingPhaseGrid");
        trace_uniforms.moving_depth_block = trace_program.uniform("uMovingDepthBlock");
        trace_uniforms.has_light = trace_program.uniform("uHasLight");
        trace_uniforms.light_position = trace_program.uniform("uLightPosition");
        trace_uniforms.light_color = trace_program.uniform("uLightColor");
        trace_uniforms.light_intensity = trace_program.uniform("uLightIntensity");
        trace_uniforms.alpha_cutoff = trace_program.uniform("uAlphaCutoff");

        trace_program.use();
        for (std::size_t slot = 0u; slot < trace_uniforms.textures.size(); ++slot) {
            char name[32]{};
            std::snprintf(name, sizeof(name), "uTexture%zu", slot);
            trace_uniforms.textures[slot] = trace_program.uniform(name);
            setInt(trace_uniforms.textures[slot], static_cast<int>(slot));
        }

        present_uniforms.accumulation = present_program.uniform("uAccumulation");
        present_uniforms.camera_moving = present_program.uniform("uCameraMoving");
        present_uniforms.moving_phase_grid = present_program.uniform("uMovingPhaseGrid");
        present_uniforms.frame_index = present_program.uniform("uFrameIndex");
        present_uniforms.exposure = present_program.uniform("uExposure");
        present_program.use();
        setInt(present_uniforms.accumulation, 0);
        Systems::OpenGL::unbindProgram();
    }

    bool createPrograms()
    {
        if (!trace_program.createCompute(PathTracerShaders::trace, "PathTracer")) return false;
        if (!present_program.createGraphics(
                PathTracerShaders::present_vertex,
                PathTracerShaders::present_fragment,
                "PathTracer"))
        {
            trace_program.destroy();
            return false;
        }
        cacheUniforms();
        return true;
    }

    void destroyPrograms()
    {
        trace_program.destroy();
        present_program.destroy();
    }

    bool syncSceneIfNeeded(const Ecs::World& world)
    {
        const std::uint64_t revision = world.changeRevision();
        if (revision == world_revision && resources.ready()) return true;

        Visibility::system().collectVisibleRenderItems(world, width, height, render_items);
        const std::uint64_t signature = scene.signature(world, render_items);
        if (signature != scene_signature || !resources.ready()) {
            std::string error;
            if (!scene.sync(
                    world,
                    render_items,
                    Systems::OpenGLSceneResources::MaximumTextureSlots,
                    &error))
            {
                std::fprintf(stderr, "[PathTracer]: scene cache failed: %s\n", error.c_str());
                return false;
            }
            if (!resources.sync(scene, &error)) {
                std::fprintf(stderr, "[PathTracer]: OpenGL scene upload failed: %s\n", error.c_str());
                return false;
            }
            scene_signature = signature;
            progressive.sceneChanged();
            std::fprintf(
                stderr,
                "[PathTracer]: world cache %zu triangles, %zu nodes, %zu materials\n",
                scene.triangles().size(),
                scene.nodes().size(),
                scene.materials().size()
            );
        }
        world_revision = revision;
        return true;
    }

    void dispatch(const Systems::CameraState& camera, const Systems::LightState& light)
    {
        const int samples = settings.samples_per_frame;
        trace_program.use();
        setVec2(trace_uniforms.resolution, static_cast<float>(trace_width), static_cast<float>(trace_height));
        setVec3(trace_uniforms.camera_position, camera.position);
        setVec3(trace_uniforms.camera_forward, camera.forward);
        setVec3(trace_uniforms.camera_right, camera.right);
        setVec3(trace_uniforms.camera_up, camera.up);
        setFloat(trace_uniforms.tan_half_fov, std::tan(camera.fov_degrees * (kPi / 360.0f)));
        setFloat(trace_uniforms.aspect, static_cast<float>(width) / static_cast<float>(height));
        setInt(trace_uniforms.node_count, static_cast<int>(scene.nodes().size()));
        setInt(trace_uniforms.triangle_count, static_cast<int>(scene.triangles().size()));
        setInt(trace_uniforms.material_count, static_cast<int>(scene.materials().size()));
        setInt(trace_uniforms.samples_this_frame, samples);
        setInt(trace_uniforms.sample_base, static_cast<int>(progressive.sampleCount()));
        setInt(trace_uniforms.frame_index, static_cast<int>(progressive.frameIndex()));
        setInt(trace_uniforms.reset_accumulation, progressive.resetPending() ? 1 : 0);
        setInt(trace_uniforms.camera_moving, progressive.cameraMoving() ? 1 : 0);
        setInt(trace_uniforms.stationary_phase_grid, settings.stationary_phase_grid);
        setInt(trace_uniforms.reset_phase_grid, settings.reset_phase_grid);
        setInt(trace_uniforms.moving_phase_grid, settings.moving_phase_grid);
        setInt(trace_uniforms.moving_depth_block, settings.moving_depth_block);
        setInt(trace_uniforms.has_light, light.valid && light.type == LightType::Point ? 1 : 0);
        setVec3(trace_uniforms.light_position, light.position);
        setVec3(trace_uniforms.light_color, light.color);
        setFloat(trace_uniforms.light_intensity, light.intensity);
        setFloat(trace_uniforms.alpha_cutoff, Systems::SceneCache::opacityCutoff());

        resources.bind();
        GL42.glBindImageTexture(0u, accumulation, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
        GL42.glBindImageTexture(1u, primary_depth, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        GL43.glDispatchCompute(
            static_cast<GLuint>((trace_width + 7) / 8),
            static_cast<GLuint>((trace_height + 7) / 8),
            1u
        );
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        Systems::OpenGL::unbindProgram();
        progressive.advance(static_cast<std::uint32_t>(samples));
    }

    void compose()
    {
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        present_program.use();
        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accumulation);
        setInt(present_uniforms.camera_moving, progressive.cameraMoving() ? 1 : 0);
        setInt(present_uniforms.moving_phase_grid, settings.moving_phase_grid);
        setInt(present_uniforms.frame_index, static_cast<int>(progressive.frameIndex()));
        setFloat(present_uniforms.exposure, settings.exposure);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(3.0f, -1.0f);
        glVertex2f(-1.0f, 3.0f);
        glEnd();
        Systems::OpenGL::unbindProgram();
    }
};

PathTracer::PathTracer() : impl_(new Impl) {}

PathTracer::~PathTracer()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool PathTracer::init()
{
    if (impl_->initialized) return true;
    if (!impl_->configured()) {
        std::fprintf(stderr, "[PathTracer]: configure the renderer before init\n");
        return false;
    }
    if (!lwcglModernGLAvailable() && lwcglLoadModernGL() != 0) {
        std::fprintf(stderr, "[PathTracer]: modern OpenGL unavailable\n");
        return false;
    }

    const int major = lwcglModernGLMajorVersion();
    const int minor = lwcglModernGLMinorVersion();
    if (major < 4 || (major == 4 && minor < 3) ||
        !GL43.glDispatchCompute || !GL42.glBindImageTexture || !GL30.glBindBufferBase)
    {
        std::fprintf(
            stderr,
            "[PathTracer]: OpenGL 4.3 compatibility context required; found %d.%d\n",
            major,
            minor
        );
        return false;
    }

    impl_->width = std::max(Display.getWidth(), 1);
    impl_->height = std::max(Display.getHeight(), 1);
    impl_->updateTraceResolution();
    if (!impl_->createPrograms() || !impl_->createTraceTargets()) {
        shutdown();
        return false;
    }

    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    impl_->initialized = true;
    std::fprintf(
        stderr,
        "[PathTracer]: OpenGL %d.%d, output %dx%d, trace %dx%d\n",
        major,
        minor,
        impl_->width,
        impl_->height,
        impl_->trace_width,
        impl_->trace_height
    );
    return true;
}

bool PathTracer::activate()
{
    return impl_ && impl_->initialized;
}

void PathTracer::deactivate()
{
}

void PathTracer::resize(int width, int height)
{
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (!impl_->initialized) return;
    if (previous_width == impl_->trace_width && previous_height == impl_->trace_height) {
        impl_->progressive.resetAccumulation();
        return;
    }
    impl_->destroyTraceTargets();
    if (!impl_->createTraceTargets()) shutdown();
}

bool PathTracer::renderScene(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (!impl_->initialized) return false;
    output.api = Internal::GraphicsApi::OpenGL;
    output.depth = Internal::DepthSource::None;
    output.width = impl_->width;
    output.height = impl_->height;
    output.command = nullptr;
    output.depth_texture = nullptr;

    if (!impl_->active()) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return true;
    }

    const int previous_width = impl_->trace_width;
    const int previous_height = impl_->trace_height;
    impl_->updateTraceResolution();
    if (previous_width != impl_->trace_width || previous_height != impl_->trace_height) {
        impl_->destroyTraceTargets();
        if (!impl_->createTraceTargets()) return false;
    }

    if (!impl_->syncSceneIfNeeded(world)) return false;
    const Systems::CameraState camera = Systems::cameraState(Systems::Scene::cameraState(world));
    if (!camera.valid) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return true;
    }
    const Systems::LightState light = Systems::lightState(Systems::Scene::lightState(world));

    impl_->progressive.updateCamera(Systems::cameraSignature(camera));
    impl_->progressive.updateLight(Systems::lightSignature(light));
    impl_->progressive.updateGlobalIllumination(
        globalIlluminationSignature(output.global_illumination)
    );

    Internal::bindGlobalIlluminationOpenGL(output.global_illumination);
    impl_->dispatch(camera, light);
    impl_->compose();

    output.depth = Internal::DepthSource::LinearTexture;
    output.depth_texture = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(impl_->primary_depth)
    );
    return true;
}

void PathTracer::present(Internal::FrameOutput& output)
{
    (void)output;
}

void PathTracer::shutdown()
{
    if (!impl_) return;
    Internal::shutdownFonts(Internal::GraphicsApi::OpenGL);
    Internal::shutdownGlobalIlluminationOpenGL();
    impl_->resources.clear();
    impl_->scene.clear();
    impl_->destroyTraceTargets();
    impl_->destroyPrograms();
    impl_->render_items.clear();
    impl_->world_revision = std::numeric_limits<std::uint64_t>::max();
    impl_->scene_signature = 0u;
    impl_->initialized = false;
    impl_->progressive.reset();
}

bool PathTracer::initialized() const
{
    return impl_ && impl_->initialized;
}

bool PathTracer::enabled() const
{
    return impl_ && impl_->settings.enabled;
}

void PathTracer::setEnabled(bool enabled)
{
    if (!impl_) return;
    impl_->settings.enabled = enabled;
    impl_->progressive.resetAccumulation();
}

PathTracerSettings& PathTracer::settings()
{
    return impl_->settings;
}

const PathTracerSettings& PathTracer::settings() const
{
    return impl_->settings;
}

} // namespace Renderer

#endif
