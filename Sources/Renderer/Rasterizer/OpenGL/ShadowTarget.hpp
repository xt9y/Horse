#ifndef RW_ENGINE_RENDERER_RASTERIZER_OPENGL_SHADOW_TARGET_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_OPENGL_SHADOW_TARGET_HPP

#include <cstddef>

namespace Renderer::RasterizerOpenGL {

class ShadowTarget {
public:
    ShadowTarget() = default;
    ~ShadowTarget();

    ShadowTarget(const ShadowTarget&) = delete;
    ShadowTarget& operator=(const ShadowTarget&) = delete;

    bool loadFramebufferApi();
    bool framebufferAvailable() const;
    void disableFramebuffer();

    bool ensure(int requested_size);
    void clear();

    bool offscreen() const;
    int size() const;

    void unbind(int first_texture_unit) const;
    void begin() const;
    void selectFace(std::size_t face) const;
    void captureFace(std::size_t face) const;
    void finish() const;
    void bind(int first_texture_unit) const;

private:
    using GenFramebuffersProc = void (*)(int, unsigned int *);
    using DeleteFramebuffersProc = void (*)(int, const unsigned int *);
    using BindFramebufferProc = void (*)(unsigned int, unsigned int);
    using FramebufferTexture2DProc = void (*)(unsigned int, unsigned int, unsigned int, unsigned int, int);
    using CheckFramebufferStatusProc = unsigned int (*)(unsigned int);
    using GenRenderbuffersProc = void (*)(int, unsigned int *);
    using DeleteRenderbuffersProc = void (*)(int, const unsigned int *);
    using BindRenderbufferProc = void (*)(unsigned int, unsigned int);
    using RenderbufferStorageProc = void (*)(unsigned int, unsigned int, int, int);
    using FramebufferRenderbufferProc = void (*)(unsigned int, unsigned int, unsigned int, unsigned int);

    bool createFramebuffer();
    void clearFramebuffer();

    unsigned int textures_[6]{};
    int size_ = 0;

    GenFramebuffersProc gl_gen_framebuffers_ = nullptr;
    DeleteFramebuffersProc gl_delete_framebuffers_ = nullptr;
    BindFramebufferProc gl_bind_framebuffer_ = nullptr;
    FramebufferTexture2DProc gl_framebuffer_texture_2d_ = nullptr;
    CheckFramebufferStatusProc gl_check_framebuffer_status_ = nullptr;
    GenRenderbuffersProc gl_gen_renderbuffers_ = nullptr;
    DeleteRenderbuffersProc gl_delete_renderbuffers_ = nullptr;
    BindRenderbufferProc gl_bind_renderbuffer_ = nullptr;
    RenderbufferStorageProc gl_renderbuffer_storage_ = nullptr;
    FramebufferRenderbufferProc gl_framebuffer_renderbuffer_ = nullptr;

    unsigned int framebuffer_ = 0u;
    unsigned int depth_renderbuffer_ = 0u;
    bool framebuffer_available_ = false;
};

} // namespace Renderer::RasterizerOpenGL

#endif
