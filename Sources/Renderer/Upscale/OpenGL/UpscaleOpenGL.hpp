#ifndef HORSE_RENDERER_UPSCALE_OPENGL_HPP
#define HORSE_RENDERER_UPSCALE_OPENGL_HPP

#include "Renderer/Quality/ScaledPass.hpp"

#include <cstdint>

namespace Renderer::Upscale {

struct OpenGLInput {
    unsigned int color_texture = 0u;
    unsigned int depth_texture = 0u;
    unsigned int full_depth_texture = 0u;
    unsigned int effect_texture = 0u;
    int source_width = 1;
    int source_height = 1;
    int effect_width = 1;
    int effect_height = 1;
    int output_width = 1;
    int output_height = 1;
    float near_plane = 0.1f;
    std::uint64_t temporal_signature = 0u;
};

struct Statistics {
    bool active = false;
    bool depth_aware = false;
    bool temporal_history = false;
    bool effect = false;
    int source_width = 0;
    int source_height = 0;
    int output_width = 0;
    int output_height = 0;
};

class OpenGLPass {
public:
    OpenGLPass();
    ~OpenGLPass();

    OpenGLPass(const OpenGLPass&) = delete;
    OpenGLPass& operator=(const OpenGLPass&) = delete;

    bool render(const OpenGLInput& input, const Quality::ScaledPassSettings& settings);
    void resetHistory();
    void shutdown();
    Statistics statistics() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::Upscale

#endif
