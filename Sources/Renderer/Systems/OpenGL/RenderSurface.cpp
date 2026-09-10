#include "Renderer/Systems/OpenGL/RenderSurface.hpp"

#include <lwcgl/lwcgl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <utility>

#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT 0x8D40
#endif
#ifndef GL_RENDERBUFFER_EXT
#define GL_RENDERBUFFER_EXT 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0_EXT
#define GL_COLOR_ATTACHMENT0_EXT 0x8CE0
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
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB 0x881A
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer::Systems::OpenGL {
namespace {

using GenFramebuffersProc = void (*)(GLsizei, GLuint *);
using DeleteFramebuffersProc = void (*)(GLsizei, const GLuint *);
using BindFramebufferProc = void (*)(GLenum, GLuint);
using FramebufferTexture2DProc = void (*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferStatusProc = GLenum (*)(GLenum);
using GenRenderbuffersProc = void (*)(GLsizei, GLuint *);
using DeleteRenderbuffersProc = void (*)(GLsizei, const GLuint *);
using BindRenderbufferProc = void (*)(GLenum, GLuint);
using RenderbufferStorageProc = void (*)(GLenum, GLenum, GLsizei, GLsizei);
using FramebufferRenderbufferProc = void (*)(GLenum, GLenum, GLenum, GLuint);

struct Api {
    GenFramebuffersProc gen_framebuffers = nullptr;
    DeleteFramebuffersProc delete_framebuffers = nullptr;
    BindFramebufferProc bind_framebuffer = nullptr;
    FramebufferTexture2DProc framebuffer_texture_2d = nullptr;
    CheckFramebufferStatusProc check_framebuffer_status = nullptr;
    GenRenderbuffersProc gen_renderbuffers = nullptr;
    DeleteRenderbuffersProc delete_renderbuffers = nullptr;
    BindRenderbufferProc bind_renderbuffer = nullptr;
    RenderbufferStorageProc renderbuffer_storage = nullptr;
    FramebufferRenderbufferProc framebuffer_renderbuffer = nullptr;
    bool loaded = false;
    bool available = false;
};

Api api;

GLFWglproc resolve(const char *core, const char *extension)
{
    GLFWglproc result = glfwGetProcAddress(core);
    if (!result) result = glfwGetProcAddress(extension);
    return result;
}

bool loadApi()
{
    if (api.loaded) return api.available;
    api.loaded = true;
    api.gen_framebuffers = reinterpret_cast<GenFramebuffersProc>(
        resolve("glGenFramebuffers", "glGenFramebuffersEXT"));
    api.delete_framebuffers = reinterpret_cast<DeleteFramebuffersProc>(
        resolve("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    api.bind_framebuffer = reinterpret_cast<BindFramebufferProc>(
        resolve("glBindFramebuffer", "glBindFramebufferEXT"));
    api.framebuffer_texture_2d = reinterpret_cast<FramebufferTexture2DProc>(
        resolve("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    api.check_framebuffer_status = reinterpret_cast<CheckFramebufferStatusProc>(
        resolve("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
    api.gen_renderbuffers = reinterpret_cast<GenRenderbuffersProc>(
        resolve("glGenRenderbuffers", "glGenRenderbuffersEXT"));
    api.delete_renderbuffers = reinterpret_cast<DeleteRenderbuffersProc>(
        resolve("glDeleteRenderbuffers", "glDeleteRenderbuffersEXT"));
    api.bind_renderbuffer = reinterpret_cast<BindRenderbufferProc>(
        resolve("glBindRenderbuffer", "glBindRenderbufferEXT"));
    api.renderbuffer_storage = reinterpret_cast<RenderbufferStorageProc>(
        resolve("glRenderbufferStorage", "glRenderbufferStorageEXT"));
    api.framebuffer_renderbuffer = reinterpret_cast<FramebufferRenderbufferProc>(
        resolve("glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT"));

    api.available = api.gen_framebuffers && api.delete_framebuffers && api.bind_framebuffer &&
        api.framebuffer_texture_2d && api.check_framebuffer_status &&
        api.gen_renderbuffers && api.delete_renderbuffers && api.bind_renderbuffer &&
        api.renderbuffer_storage && api.framebuffer_renderbuffer;
    return api.available;
}

} // namespace

bool framebufferApiAvailable()
{
    return loadApi();
}

RenderSurface::~RenderSurface()
{
    clear();
}

RenderSurface::RenderSurface(RenderSurface&& other) noexcept
{
    *this = std::move(other);
}

RenderSurface& RenderSurface::operator=(RenderSurface&& other) noexcept
{
    if (this == &other) return *this;
    clear();
    framebuffer_ = std::exchange(other.framebuffer_, 0u);
    color_texture_ = std::exchange(other.color_texture_, 0u);
    depth_texture_ = std::exchange(other.depth_texture_, 0u);
    depth_renderbuffer_ = std::exchange(other.depth_renderbuffer_, 0u);
    width_ = std::exchange(other.width_, 0);
    height_ = std::exchange(other.height_, 0);
    format_ = other.format_;
    sampleable_depth_ = other.sampleable_depth_;
    return *this;
}

bool RenderSurface::ensure(
    int width,
    int height,
    SurfaceColorFormat format,
    bool sampleable_depth)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (ready() && width_ == width && height_ == height &&
        format_ == format && sampleable_depth_ == sampleable_depth)
    {
        return true;
    }
    clear();
    if (!loadApi()) return false;

    width_ = width;
    height_ = height;
    format_ = format;
    sampleable_depth_ = sampleable_depth;

    glGenTextures(1, reinterpret_cast<GLuint *>(&color_texture_));
    if (color_texture_ == 0u) {
        clear();
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        format == SurfaceColorFormat::Rgba16Float ? GL_RGBA16F_ARB : GL_RGBA,
        width_,
        height_,
        0,
        GL_RGBA,
        format == SurfaceColorFormat::Rgba16Float ? GL_FLOAT : GL_UNSIGNED_BYTE,
        nullptr
    );

    if (sampleable_depth_) {
        glGenTextures(1, reinterpret_cast<GLuint *>(&depth_texture_));
        if (depth_texture_ == 0u) {
            clear();
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, depth_texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_DEPTH_COMPONENT24,
            width_,
            height_,
            0,
            GL_DEPTH_COMPONENT,
            GL_UNSIGNED_INT,
            nullptr
        );
    } else {
        api.gen_renderbuffers(1, reinterpret_cast<GLuint *>(&depth_renderbuffer_));
        if (depth_renderbuffer_ == 0u) {
            clear();
            return false;
        }
        api.bind_renderbuffer(GL_RENDERBUFFER_EXT, depth_renderbuffer_);
        api.renderbuffer_storage(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, width_, height_);
        api.bind_renderbuffer(GL_RENDERBUFFER_EXT, 0u);
    }

    api.gen_framebuffers(1, reinterpret_cast<GLuint *>(&framebuffer_));
    if (framebuffer_ == 0u) {
        clear();
        return false;
    }
    api.bind_framebuffer(GL_FRAMEBUFFER_EXT, framebuffer_);
    api.framebuffer_texture_2d(
        GL_FRAMEBUFFER_EXT,
        GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_2D,
        color_texture_,
        0
    );
    if (sampleable_depth_) {
        api.framebuffer_texture_2d(
            GL_FRAMEBUFFER_EXT,
            GL_DEPTH_ATTACHMENT_EXT,
            GL_TEXTURE_2D,
            depth_texture_,
            0
        );
    } else {
        api.framebuffer_renderbuffer(
            GL_FRAMEBUFFER_EXT,
            GL_DEPTH_ATTACHMENT_EXT,
            GL_RENDERBUFFER_EXT,
            depth_renderbuffer_
        );
    }
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    const GLenum status = api.check_framebuffer_status(GL_FRAMEBUFFER_EXT);
    api.bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
    glDrawBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, 0u);

    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        clear();
        return false;
    }
    return true;
}

bool RenderSurface::bind() const
{
    if (!ready() || !loadApi()) return false;
    api.bind_framebuffer(GL_FRAMEBUFFER_EXT, framebuffer_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glViewport(0, 0, width_, height_);
    return true;
}

void RenderSurface::unbind()
{
    if (!loadApi()) return;
    api.bind_framebuffer(GL_FRAMEBUFFER_EXT, 0u);
    glDrawBuffer(GL_BACK);
}

bool RenderSurface::copyColorFromFramebuffer() const
{
    if (!ready()) return false;
    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);
    glBindTexture(GL_TEXTURE_2D, 0u);
    return glGetError() == GL_NO_ERROR;
}

bool RenderSurface::copyDepthFromFramebuffer() const
{
    if (!ready() || depth_texture_ == 0u) return false;
    glBindTexture(GL_TEXTURE_2D, depth_texture_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);
    glBindTexture(GL_TEXTURE_2D, 0u);
    return glGetError() == GL_NO_ERROR;
}

void RenderSurface::clear()
{
    if (framebuffer_ != 0u && loadApi()) api.delete_framebuffers(1, reinterpret_cast<GLuint *>(&framebuffer_));
    if (depth_renderbuffer_ != 0u && loadApi())
        api.delete_renderbuffers(1, reinterpret_cast<GLuint *>(&depth_renderbuffer_));
    if (color_texture_ != 0u) glDeleteTextures(1, reinterpret_cast<GLuint *>(&color_texture_));
    if (depth_texture_ != 0u) glDeleteTextures(1, reinterpret_cast<GLuint *>(&depth_texture_));
    framebuffer_ = 0u;
    color_texture_ = 0u;
    depth_texture_ = 0u;
    depth_renderbuffer_ = 0u;
    width_ = 0;
    height_ = 0;
}

} // namespace Renderer::Systems::OpenGL
