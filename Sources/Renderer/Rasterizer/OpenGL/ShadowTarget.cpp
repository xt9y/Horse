#include "Renderer/Rasterizer/OpenGL/ShadowTarget.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
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

namespace Renderer::RasterizerOpenGL {
namespace {

GLFWglproc resolveFramebufferProc(const char *core, const char *extension)
{
    GLFWglproc result = glfwGetProcAddress(core);
    if (!result) result = glfwGetProcAddress(extension);
    return result;
}

} // namespace

ShadowTarget::~ShadowTarget()
{
    clear();
}

bool ShadowTarget::loadFramebufferApi()
{
    gl_gen_framebuffers_ = reinterpret_cast<GenFramebuffersProc>(resolveFramebufferProc("glGenFramebuffers", "glGenFramebuffersEXT"));
    gl_delete_framebuffers_ = reinterpret_cast<DeleteFramebuffersProc>(resolveFramebufferProc("glDeleteFramebuffers", "glDeleteFramebuffersEXT"));
    gl_bind_framebuffer_ = reinterpret_cast<BindFramebufferProc>(resolveFramebufferProc("glBindFramebuffer", "glBindFramebufferEXT"));
    gl_framebuffer_texture_2d_ = reinterpret_cast<FramebufferTexture2DProc>(resolveFramebufferProc("glFramebufferTexture2D", "glFramebufferTexture2DEXT"));
    gl_check_framebuffer_status_ = reinterpret_cast<CheckFramebufferStatusProc>(resolveFramebufferProc("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT"));
    gl_gen_renderbuffers_ = reinterpret_cast<GenRenderbuffersProc>(resolveFramebufferProc("glGenRenderbuffers", "glGenRenderbuffersEXT"));
    gl_delete_renderbuffers_ = reinterpret_cast<DeleteRenderbuffersProc>(resolveFramebufferProc("glDeleteRenderbuffers", "glDeleteRenderbuffersEXT"));
    gl_bind_renderbuffer_ = reinterpret_cast<BindRenderbufferProc>(resolveFramebufferProc("glBindRenderbuffer", "glBindRenderbufferEXT"));
    gl_renderbuffer_storage_ = reinterpret_cast<RenderbufferStorageProc>(resolveFramebufferProc("glRenderbufferStorage", "glRenderbufferStorageEXT"));
    gl_framebuffer_renderbuffer_ = reinterpret_cast<FramebufferRenderbufferProc>(resolveFramebufferProc("glFramebufferRenderbuffer", "glFramebufferRenderbufferEXT"));

    framebuffer_available_ = gl_gen_framebuffers_ && gl_delete_framebuffers_ && gl_bind_framebuffer_ &&
        gl_framebuffer_texture_2d_ && gl_check_framebuffer_status_ && gl_gen_renderbuffers_ &&
        gl_delete_renderbuffers_ && gl_bind_renderbuffer_ && gl_renderbuffer_storage_ && gl_framebuffer_renderbuffer_;
    return framebuffer_available_;
}

bool ShadowTarget::framebufferAvailable() const
{
    return framebuffer_available_;
}

void ShadowTarget::disableFramebuffer()
{
    clearFramebuffer();
    framebuffer_available_ = false;
}

bool ShadowTarget::createFramebuffer()
{
    if (!framebuffer_available_) return false;

    gl_gen_framebuffers_(1, &framebuffer_);
    gl_gen_renderbuffers_(1, &depth_renderbuffer_);
    if (framebuffer_ == 0u || depth_renderbuffer_ == 0u) {
        clearFramebuffer();
        return false;
    }

    gl_bind_framebuffer_(GL_FRAMEBUFFER_EXT, framebuffer_);
    gl_bind_renderbuffer_(GL_RENDERBUFFER_EXT, depth_renderbuffer_);
    gl_renderbuffer_storage_(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, size_, size_);
    gl_framebuffer_renderbuffer_(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, depth_renderbuffer_);
    gl_framebuffer_texture_2d_(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, textures_[0], 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    const unsigned int status = gl_check_framebuffer_status_(GL_FRAMEBUFFER_EXT);
    gl_bind_renderbuffer_(GL_RENDERBUFFER_EXT, 0u);
    gl_bind_framebuffer_(GL_FRAMEBUFFER_EXT, 0u);
    glDrawBuffer(GL_BACK);

    if (status != GL_FRAMEBUFFER_COMPLETE_EXT) {
        clearFramebuffer();
        framebuffer_available_ = false;
        return false;
    }
    return true;
}

bool ShadowTarget::ensure(int requested_size)
{
    int maximum = requested_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    const int size = std::max(1, std::min(requested_size, maximum));
    if (textures_[0] != 0u && size_ == size) return true;

    clear();
    size_ = size;
    glGenTextures(6, textures_);
    for (const unsigned int texture : textures_) {
        if (texture == 0u) return false;
        GLModern.glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size_, size_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    if (framebuffer_available_ && !createFramebuffer()) return false;
    return true;
}

void ShadowTarget::clearFramebuffer()
{
    if (depth_renderbuffer_ != 0u && gl_delete_renderbuffers_)
        gl_delete_renderbuffers_(1, &depth_renderbuffer_);
    if (framebuffer_ != 0u && gl_delete_framebuffers_)
        gl_delete_framebuffers_(1, &framebuffer_);
    depth_renderbuffer_ = 0u;
    framebuffer_ = 0u;
}

void ShadowTarget::clear()
{
    clearFramebuffer();
    for (unsigned int& texture : textures_) {
        if (texture != 0u) glDeleteTextures(1, &texture);
        texture = 0u;
    }
    size_ = 0;
}

bool ShadowTarget::offscreen() const
{
    return framebuffer_available_ && framebuffer_ != 0u && depth_renderbuffer_ != 0u;
}

int ShadowTarget::size() const
{
    return size_;
}

void ShadowTarget::unbind(int first_texture_unit) const
{
    for (int face = 0; face < 6; ++face) {
        GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + first_texture_unit + face));
        glBindTexture(GL_TEXTURE_2D, 0u);
    }
    GLModern.glActiveTexture(GL_TEXTURE0);
}

void ShadowTarget::begin() const
{
    if (!offscreen()) return;
    gl_bind_framebuffer_(GL_FRAMEBUFFER_EXT, framebuffer_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
}

void ShadowTarget::selectFace(std::size_t face) const
{
    if (!offscreen() || face >= 6u) return;
    gl_framebuffer_texture_2d_(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, textures_[face], 0);
}

void ShadowTarget::captureFace(std::size_t face) const
{
    if (offscreen() || face >= 6u) return;
    GLModern.glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[face]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, size_, size_);
}

void ShadowTarget::finish() const
{
    if (!offscreen()) return;
    gl_bind_framebuffer_(GL_FRAMEBUFFER_EXT, 0u);
    glDrawBuffer(GL_BACK);
}

void ShadowTarget::bind(int first_texture_unit) const
{
    for (int face = 0; face < 6; ++face) {
        GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + first_texture_unit + face));
        glBindTexture(GL_TEXTURE_2D, textures_[face]);
    }
}

} // namespace Renderer::RasterizerOpenGL
