#include "Renderer/PostProcess/OpenGL/PostProcessOpenGL.hpp"

#include "Renderer/PostProcess/OpenGL/PostProcessShaders.hpp"
#include "Renderer/Systems/OpenGL/Program.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdio>

#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB 0x881A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER_EXT
#define GL_READ_FRAMEBUFFER_EXT 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER_EXT
#define GL_DRAW_FRAMEBUFFER_EXT 0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
#endif
#ifndef GL_COLOR_ATTACHMENT1_EXT
#define GL_COLOR_ATTACHMENT1_EXT 0x8CE1
#endif
#ifndef GL_DEPTH_ATTACHMENT_EXT
#define GL_DEPTH_ATTACHMENT_EXT 0x8D00
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_COMPLETE_EXT 0x8CD5
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

namespace Renderer::PostProcess::OpenGL {
namespace {

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

void fullscreenTriangle()
{
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(3.0f, -1.0f);
    glVertex2f(-1.0f, 3.0f);
    glEnd();
}

} // namespace

struct Pipeline::Impl {
    using GenFramebuffersProc = void (*)(GLsizei, GLuint *);
    using DeleteFramebuffersProc = void (*)(GLsizei, const GLuint *);
    using BindFramebufferProc = void (*)(GLenum, GLuint);
    using FramebufferTexture2DProc = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferStatusProc = GLenum (*)(GLenum);
    using DrawBuffersProc = void (*)(GLsizei, const GLenum *);
    using BlitFramebufferProc = void (*)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

    GenFramebuffersProc gen_framebuffers = nullptr;
    DeleteFramebuffersProc delete_framebuffers = nullptr;
    BindFramebufferProc bind_framebuffer = nullptr;
    FramebufferTexture2DProc framebuffer_texture_2d = nullptr;
    CheckFramebufferStatusProc check_framebuffer = nullptr;
    DrawBuffersProc draw_buffers = nullptr;
    BlitFramebufferProc blit_framebuffer = nullptr;

    Systems::OpenGL::Program extract_program;
    Systems::OpenGL::Program blur_program;
    Systems::OpenGL::Program present_program;

    GLuint scene_fbo = 0u;
    GLuint bloom_fbo = 0u;
    GLuint scene_color = 0u;
    GLuint velocity = 0u;
    GLuint depth = 0u;
    GLuint bloom_a = 0u;
    GLuint bloom_b = 0u;
    int width = 1;
    int height = 1;
    int bloom_width = 1;
    int bloom_height = 1;
    bool initialized = false;
    bool complete = false;
    bool depth_copied = false;

    struct ExtractUniforms {
        GLint scene = -1;
        GLint threshold = -1;
    } extract;
    struct BlurUniforms {
        GLint source = -1;
        GLint texel = -1;
        GLint direction = -1;
    } blur;
    struct PresentUniforms {
        GLint scene = -1;
        GLint bloom = -1;
        GLint velocity = -1;
        GLint texel = -1;
        GLint exposure = -1;
        GLint bloom_intensity = -1;
        GLint motion_strength = -1;
        GLint motion_samples = -1;
        GLint motion_enabled = -1;
        GLint fxaa_enabled = -1;
    } present;

    static GLFWglproc resolve(const char *core, const char *extension)
    {
        GLFWglproc proc = glfwGetProcAddress(core);
        if (!proc && extension) proc = glfwGetProcAddress(extension);
        return proc;
    }

    bool loadApi()
    {
        gen_framebuffers = reinterpret_cast<GenFramebuffersProc>(resolve("glGenFramebuffers", "glGenFramebuffersEXT"));
        delete_framebuffers = reinterpret_cast<DeleteFramebuffersProc>(resolve("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
        bind_framebuffer = reinterpret_cast<BindFramebufferProc>(resolve("glBindFramebuffer", "glBindFramebufferEXT"));
        framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DProc>(resolve("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
        check_framebuffer = reinterpret_cast<CheckFramebufferStatusProc>(resolve("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
        draw_buffers = reinterpret_cast<DrawBuffersProc>(resolve("glDrawBuffers", "glDrawBuffersARB"));
        blit_framebuffer = reinterpret_cast<BlitFramebufferProc>(resolve("glBlitFramebuffer", "glBlitFramebufferEXT"));
        return gen_framebuffers && delete_framebuffers && bind_framebuffer &&
            framebuffer_texture_2d && check_framebuffer && draw_buffers;
    }

    bool createPrograms()
    {
        if (!extract_program.createGraphics(PostProcessShaders::vertex, PostProcessShaders::extract, "Post Bloom Extract"))
            return false;
        if (!blur_program.createGraphics(PostProcessShaders::vertex, PostProcessShaders::blur, "Post Bloom Blur"))
            return false;
        if (!present_program.createGraphics(PostProcessShaders::vertex, PostProcessShaders::present, "Post Present"))
            return false;

        extract.scene = extract_program.uniform("uScene");
        extract.threshold = extract_program.uniform("uThreshold");
        blur.source = blur_program.uniform("uSource");
        blur.texel = blur_program.uniform("uTexel");
        blur.direction = blur_program.uniform("uDirection");
        present.scene = present_program.uniform("uScene");
        present.bloom = present_program.uniform("uBloom");
        present.velocity = present_program.uniform("uVelocity");
        present.texel = present_program.uniform("uTexel");
        present.exposure = present_program.uniform("uExposure");
        present.bloom_intensity = present_program.uniform("uBloomIntensity");
        present.motion_strength = present_program.uniform("uMotionBlurStrength");
        present.motion_samples = present_program.uniform("uMotionBlurSamples");
        present.motion_enabled = present_program.uniform("uMotionBlurEnabled");
        present.fxaa_enabled = present_program.uniform("uFxaaEnabled");

        extract_program.use(); setInt(extract.scene, 0);
        blur_program.use(); setInt(blur.source, 0);
        present_program.use();
        setInt(present.scene, 0);
        setInt(present.bloom, 1);
        setInt(present.velocity, 2);
        Systems::OpenGL::unbindProgram();
        return true;
    }

    void destroyPrograms()
    {
        extract_program.destroy();
        blur_program.destroy();
        present_program.destroy();
    }

    void destroyTargets()
    {
        const GLuint textures[] = {scene_color, velocity, depth, bloom_a, bloom_b};
        for (GLuint texture : textures) if (texture != 0u) glDeleteTextures(1, &texture);
        scene_color = velocity = depth = bloom_a = bloom_b = 0u;
        if (scene_fbo != 0u && delete_framebuffers) delete_framebuffers(1, &scene_fbo);
        if (bloom_fbo != 0u && delete_framebuffers) delete_framebuffers(1, &bloom_fbo);
        scene_fbo = bloom_fbo = 0u;
        complete = false;
    }

    bool createColorTexture(GLuint& target, int w, int h, GLint filter)
    {
        glGenTextures(1, &target);
        if (target == 0u) return false;
        glBindTexture(GL_TEXTURE_2D, target);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_ARB, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        return true;
    }

    bool createDepthTexture()
    {
        glGenTextures(1, &depth);
        if (depth == 0u) return false;
        glBindTexture(GL_TEXTURE_2D, depth);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        return true;
    }

    bool createTargets()
    {
        destroyTargets();
        bloom_width = std::max(width / 2, 1);
        bloom_height = std::max(height / 2, 1);
        if (!createColorTexture(scene_color, width, height, GL_LINEAR) ||
            !createColorTexture(velocity, width, height, GL_NEAREST) ||
            !createDepthTexture() ||
            !createColorTexture(bloom_a, bloom_width, bloom_height, GL_LINEAR) ||
            !createColorTexture(bloom_b, bloom_width, bloom_height, GL_LINEAR))
        {
            destroyTargets();
            return false;
        }

        gen_framebuffers(1, &scene_fbo);
        gen_framebuffers(1, &bloom_fbo);
        if (scene_fbo == 0u || bloom_fbo == 0u) {
            destroyTargets();
            return false;
        }

        bind_framebuffer(GL_FRAMEBUFFER_EXT, scene_fbo);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, scene_color, 0);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, velocity, 0);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, depth, 0);
        const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT};
        draw_buffers(2, attachments);
        const bool scene_ok = check_framebuffer(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT;

        bind_framebuffer(GL_FRAMEBUFFER_EXT, bloom_fbo);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, bloom_a, 0);
        const GLenum bloom_attachment = GL_COLOR_ATTACHMENT0_EXT;
        draw_buffers(1, &bloom_attachment);
        const bool bloom_ok = check_framebuffer(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT;
        bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
        glDrawBuffer(GL_BACK);
        complete = scene_ok && bloom_ok;
        if (!complete) destroyTargets();
        return complete;
    }

    void bindTexture(int unit, GLuint texture)
    {
        GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    void renderBloom(const PostProcessState& state)
    {
        if (!state.bloom || state.bloom_intensity <= 0.0f) return;
        bind_framebuffer(GL_FRAMEBUFFER_EXT, bloom_fbo);
        glViewport(0, 0, bloom_width, bloom_height);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, bloom_a, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        extract_program.use();
        bindTexture(0, scene_color);
        setFloat(extract.threshold, state.bloom_threshold);
        fullscreenTriangle();

        blur_program.use();
        setVec2(blur.texel, 1.0f / static_cast<float>(bloom_width), 1.0f / static_cast<float>(bloom_height));
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, bloom_b, 0);
        bindTexture(0, bloom_a);
        setVec2(blur.direction, 1.0f, 0.0f);
        fullscreenTriangle();
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, bloom_a, 0);
        bindTexture(0, bloom_b);
        setVec2(blur.direction, 0.0f, 1.0f);
        fullscreenTriangle();
    }

    bool presentFrame(const PostProcessState& state)
    {
        renderBloom(state);
        bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
        glDrawBuffer(GL_BACK);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        present_program.use();
        bindTexture(0, scene_color);
        bindTexture(1, bloom_a);
        bindTexture(2, velocity);
        setVec2(present.texel, 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
        setFloat(present.exposure, state.exposure);
        setFloat(present.bloom_intensity, state.bloom ? state.bloom_intensity : 0.0f);
        setFloat(present.motion_strength, state.motion_blur_strength);
        setInt(present.motion_samples, static_cast<int>(state.motion_blur_samples));
        setInt(present.motion_enabled, state.motion_blur ? 1 : 0);
        setInt(present.fxaa_enabled, state.anti_aliasing == AntiAliasing::Fxaa ? 1 : 0);
        fullscreenTriangle();
        Systems::OpenGL::unbindProgram();
        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0u);

        depth_copied = false;
        if (blit_framebuffer) {
            bind_framebuffer(GL_READ_FRAMEBUFFER_EXT, scene_fbo);
            bind_framebuffer(GL_DRAW_FRAMEBUFFER_EXT, 0u);
            blit_framebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
            depth_copied = true;
        }
        return true;
    }
};

Pipeline::Pipeline() : impl_(new Impl) {}

Pipeline::~Pipeline()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool Pipeline::init()
{
    if (!impl_) return false;
    if (impl_->initialized) return true;
    if (!impl_->loadApi() || !impl_->createPrograms()) return false;
    impl_->initialized = true;
    return impl_->createTargets();
}

bool Pipeline::resize(int width, int height)
{
    if (!impl_) return false;
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    return !impl_->initialized || impl_->createTargets();
}

bool Pipeline::begin()
{
    if (!impl_ || !impl_->initialized || !impl_->complete) return false;
    impl_->bind_framebuffer(GL_FRAMEBUFFER_EXT, impl_->scene_fbo);
    const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT};
    impl_->draw_buffers(2, attachments);
    glViewport(0, 0, impl_->width, impl_->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    impl_->depth_copied = false;
    return true;
}

bool Pipeline::present(const PostProcessState& state)
{
    return impl_ && impl_->initialized && impl_->complete && impl_->presentFrame(state);
}

void Pipeline::shutdown()
{
    if (!impl_) return;
    impl_->destroyTargets();
    impl_->destroyPrograms();
    impl_->initialized = false;
    impl_->depth_copied = false;
}

bool Pipeline::ready() const
{
    return impl_ && impl_->initialized && impl_->complete;
}

bool Pipeline::depthCopiedToDefault() const
{
    return impl_ && impl_->depth_copied;
}

unsigned int Pipeline::depthTexture() const
{
    return impl_ ? impl_->depth : 0u;
}

unsigned int Pipeline::velocityTexture() const
{
    return impl_ ? impl_->velocity : 0u;
}

} // namespace Renderer::PostProcess::OpenGL
