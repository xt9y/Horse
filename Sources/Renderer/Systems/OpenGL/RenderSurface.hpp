#ifndef HORSE_RENDERER_SYSTEMS_OPENGL_RENDER_SURFACE_HPP
#define HORSE_RENDERER_SYSTEMS_OPENGL_RENDER_SURFACE_HPP

namespace Renderer::Systems::OpenGL {

enum class SurfaceColorFormat {
    Rgba8,
    Rgba16Float,
};

class RenderSurface {
public:
    RenderSurface() = default;
    ~RenderSurface();

    RenderSurface(const RenderSurface&) = delete;
    RenderSurface& operator=(const RenderSurface&) = delete;

    RenderSurface(RenderSurface&& other) noexcept;
    RenderSurface& operator=(RenderSurface&& other) noexcept;

    bool ensure(
        int width,
        int height,
        SurfaceColorFormat format = SurfaceColorFormat::Rgba8,
        bool sampleable_depth = true
    );
    bool bind() const;
    static void unbind();
    void clear();

    bool ready() const { return framebuffer_ != 0u && color_texture_ != 0u; }
    int width() const { return width_; }
    int height() const { return height_; }
    unsigned int colorTexture() const { return color_texture_; }
    unsigned int depthTexture() const { return depth_texture_; }
    bool hasDepthTexture() const { return depth_texture_ != 0u; }

private:
    unsigned int framebuffer_ = 0u;
    unsigned int color_texture_ = 0u;
    unsigned int depth_texture_ = 0u;
    unsigned int depth_renderbuffer_ = 0u;
    int width_ = 0;
    int height_ = 0;
    SurfaceColorFormat format_ = SurfaceColorFormat::Rgba8;
    bool sampleable_depth_ = true;
};

bool framebufferApiAvailable();

} // namespace Renderer::Systems::OpenGL

#endif
