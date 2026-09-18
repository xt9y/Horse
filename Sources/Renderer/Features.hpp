#ifndef HORSE_RENDERER_FEATURES_HPP
#define HORSE_RENDERER_FEATURES_HPP

namespace Renderer::Features {

struct Settings {
    bool lighting = true;
    bool shadows = true;
    bool environment = true;
    bool global_illumination = true;
    bool volumetrics = true;
    bool gaussian_splat = true;
};

Settings& settings();
const Settings& currentSettings();

} // namespace Renderer::Features

#endif
