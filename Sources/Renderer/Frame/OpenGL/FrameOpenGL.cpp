#include "Renderer/Frame/OpenGL/FrameOpenGL.hpp"

#include <lwcgl/glmodern.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cstdint>

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

namespace Renderer::Frame::OpenGL {
namespace {

using BindFramebufferProc = void (*)(GLenum, GLuint);

BindFramebufferProc resolveBindFramebuffer()
{
    GLFWglproc proc = glfwGetProcAddress("glBindFramebuffer");
    if (!proc) proc = glfwGetProcAddress("glBindFramebufferEXT");
    return reinterpret_cast<BindFramebufferProc>(proc);
}

} // namespace

struct Target::Impl {
    using GenFramebuffersProc = void (*)(GLsizei, GLuint *);
    using DeleteFramebuffersProc = void (*)(GLsizei, const GLuint *);
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

    GLuint framebuffer = 0u;
    GLuint color = 0u;
    GLuint velocity = 0u;
    GLuint depth = 0u;
    int width = 1;
    int height = 1;
    bool initialized = false;
    bool complete = false;

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

    bool createColor(GLuint& texture, GLint filter)
    {
        glGenTextures(1, &texture);
        if (texture == 0u) return false;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_ARB, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        return true;
    }

    bool createDepth()
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

    void destroyTargets()
    {
        const GLuint textures[] = {color, velocity, depth};
        for (GLuint texture : textures) if (texture != 0u) glDeleteTextures(1, &texture);
        color = velocity = depth = 0u;
        if (framebuffer != 0u && delete_framebuffers) delete_framebuffers(1, &framebuffer);
        framebuffer = 0u;
        complete = false;
    }

    bool createTargets()
    {
        destroyTargets();
        if (!createColor(color, GL_LINEAR) || !createColor(velocity, GL_NEAREST) || !createDepth()) {
            destroyTargets();
            return false;
        }

        gen_framebuffers(1, &framebuffer);
        if (framebuffer == 0u) {
            destroyTargets();
            return false;
        }

        bind_framebuffer(GL_FRAMEBUFFER_EXT, framebuffer);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, color, 0);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_2D, velocity, 0);
        framebuffer_texture_2d(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, depth, 0);
        const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT};
        draw_buffers(2, attachments);
        complete = check_framebuffer(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT;
        bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
        glDrawBuffer(GL_BACK);
        if (!complete) destroyTargets();
        return complete;
    }
};

Target::Target() : impl_(new Impl) {}

Target::~Target()
{
    shutdown();
    delete impl_;
}

bool Target::init()
{
    if (!impl_) return false;
    if (impl_->initialized) return impl_->complete;
    if (!impl_->loadApi() || !impl_->createTargets()) return false;
    impl_->initialized = true;
    return true;
}

bool Target::resize(int width, int height)
{
    if (!impl_) return false;
    const int next_width = std::max(width, 1);
    const int next_height = std::max(height, 1);
    if (impl_->width == next_width && impl_->height == next_height)
        return !impl_->initialized || impl_->complete;
    impl_->width = next_width;
    impl_->height = next_height;
    return !impl_->initialized || impl_->createTargets();
}

bool Target::begin()
{
    if (!impl_ || !impl_->initialized || !impl_->complete) return false;
    impl_->bind_framebuffer(GL_FRAMEBUFFER_EXT, impl_->framebuffer);
    const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT};
    impl_->draw_buffers(2, attachments);
    glViewport(0, 0, impl_->width, impl_->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

void Target::finish(Internal::FrameOutput& output)
{
    if (!impl_) return;
    impl_->bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
    glDrawBuffer(GL_BACK);
    output.color_texture = reinterpret_cast<void*>(static_cast<std::uintptr_t>(impl_->color));
    output.depth_texture = reinterpret_cast<void*>(static_cast<std::uintptr_t>(impl_->depth));
    output.velocity_texture = reinterpret_cast<void*>(static_cast<std::uintptr_t>(impl_->velocity));
    output.depth = Internal::DepthSource::Native;
}

bool Target::compose(Internal::FrameOutput& output)
{
    if (!impl_ || !impl_->initialized || !impl_->complete) return false;
    impl_->bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
    glDrawBuffer(GL_BACK);
    if (!presentTexture(output.color_texture, output.width, output.height)) return false;

    if (impl_->blit_framebuffer) {
        impl_->bind_framebuffer(GL_READ_FRAMEBUFFER_EXT, impl_->framebuffer);
        impl_->bind_framebuffer(GL_DRAW_FRAMEBUFFER_EXT, 0u);
        impl_->blit_framebuffer(
            0, 0, impl_->width, impl_->height,
            0, 0, impl_->width, impl_->height,
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST
        );
        impl_->bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
        output.depth = Internal::DepthSource::Native;
    } else {
        output.depth = Internal::DepthSource::None;
    }
    return true;
}

void Target::shutdown()
{
    if (!impl_) return;
    impl_->destroyTargets();
    impl_->initialized = false;
}

bool presentTexture(void *texture, int width, int height)
{
    const GLuint color = static_cast<GLuint>(reinterpret_cast<std::uintptr_t>(texture));
    if (color == 0u) return false;
    const BindFramebufferProc bind_framebuffer = resolveBindFramebuffer();
    if (bind_framebuffer) bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);

    glViewport(0, 0, std::max(width, 1), std::max(height, 1));
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    if (GL20.glUseProgram) GL20.glUseProgram(0u);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, color);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glEnd();

    glMatrixMode(GL_TEXTURE); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, 0u);
    glPopClientAttrib();
    glPopAttrib();
    return true;
}

} // namespace Renderer::Frame::OpenGL
