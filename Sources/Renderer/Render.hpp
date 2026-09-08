#ifndef RW_ENGINE_RENDER_HPP
#define RW_ENGINE_RENDER_HPP

#include "Ecs/Ecs.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/PathTracer/PathTracer.hpp"

#include <cstdint>
#include <unordered_map>

namespace Renderer {

class Rasterizer {
public:
    bool init();
    void resize(int width, int height);
    void render(const Ecs::World& world);
    void shutdown();

    bool initialized() const { return initialized_; }
    bool enabled() const { return path_tracer_.enabled(); }
    void setEnabled(bool enabled) { path_tracer_.setEnabled(enabled); }

    PathTracerSettings& settings() { return path_tracer_.settings(); }
    const PathTracerSettings& settings() const { return path_tracer_.settings(); }

    bool usingPathTracer() const { return use_path_tracer_; }

private:
    bool initCompatibility();
    void renderCompatibility(const Ecs::World& world);
    unsigned int textureFor(std::uint32_t handle);

    PathTracer path_tracer_{};
    bool initialized_ = false;
    bool use_path_tracer_ = false;
    int width_ = 1;
    int height_ = 1;
    std::unordered_map<std::uint32_t, unsigned int> textures_;
};

} // namespace Renderer

#endif
