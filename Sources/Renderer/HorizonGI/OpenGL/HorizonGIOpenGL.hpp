#ifndef HORSE_RENDERER_HORIZON_GI_OPENGL_HPP
#define HORSE_RENDERER_HORIZON_GI_OPENGL_HPP

#include "Renderer/HorizonGI/HorizonGI.hpp"

#include <cstdint>

namespace Renderer::HorizonGI {

struct OpenGLInput {
    unsigned int color_texture = 0u;
    unsigned int depth_texture = 0u;
    int source_width = 1;
    int source_height = 1;
    int output_width = 1;
    int output_height = 1;
    float near_plane = 0.1f;
    float tan_half_fov = 1.0f;
    float aspect = 1.0f;
    float intensity = 1.0f;
    std::uint64_t temporal_signature = 0u;
};

struct Statistics {
    bool active = false;
    bool indirect = false;
    bool temporal_history = false;
    int width = 0;
    int height = 0;
    int directions = 0;
    int steps = 0;
};

class OpenGLPass {
public:
    OpenGLPass();
    ~OpenGLPass();

    OpenGLPass(const OpenGLPass&) = delete;
    OpenGLPass& operator=(const OpenGLPass&) = delete;

    unsigned int render(const OpenGLInput& input, const Settings& settings);
    void resetHistory();
    void shutdown();

    Statistics statistics() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace Renderer::HorizonGI

#endif
