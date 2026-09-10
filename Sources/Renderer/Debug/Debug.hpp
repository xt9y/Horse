#ifndef HORSE_RENDERER_DEBUG_DEBUG_HPP
#define HORSE_RENDERER_DEBUG_DEBUG_HPP

namespace Renderer::Debug {

struct Settings {
    bool wireframe = false;
    bool show_bvh = false;
    bool show_photons = false;
    bool show_gi_probes = false;
    bool show_scene_bounds = false;
    bool show_light = false;
    bool show_normals = false;
    int bvh_level = 2;
    float overlay_opacity = 0.80f;
    float photon_size = 2.0f;
};

Settings& settings();
void clear();
void shutdown();

} // namespace Renderer::Debug

#endif
