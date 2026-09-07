#ifndef RW_ENGINE_RENDERER_GI_HPP
#define RW_ENGINE_RENDERER_GI_HPP

#include "Ecs/Ecs.hpp"

namespace Renderer {

struct GiSettings {
    int resolution_divisor = 8;
    int rays_per_pixel = 1;
    int denoise_iterations = 2;
    int max_bounces = 1;

    bool enabled = true;
    bool screen_space_first = true;
    bool bvh_fallback = true;
    bool surface_cache = false;
    bool temporal_reuse = true;
    bool denoise = true;

    float temporal_alpha = 0.16f;
    float depth_rejection = 0.02f;
    float normal_rejection = 0.85f;
};

class GI {
public:
    struct Impl;

    GI();
    ~GI();

    GI(const GI&) = delete;
    GI& operator=(const GI&) = delete;

    bool init(int width, int height);
    void resize(int width, int height);

    void begin(const Ecs::World& world);
    void bindMaterial(unsigned int texture_id);
    void end(const Ecs::World& world);

    void shutdown();

    bool initialized() const;
    bool enabled() const;
    void setEnabled(bool enabled);

    GiSettings& settings();
    const GiSettings& settings() const;

private:
    Impl *impl_;
};

} // namespace Renderer

#endif
