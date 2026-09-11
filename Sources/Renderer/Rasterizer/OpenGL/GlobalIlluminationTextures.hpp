#ifndef RW_ENGINE_RENDERER_RASTERIZER_OPENGL_GLOBAL_ILLUMINATION_TEXTURES_HPP
#define RW_ENGINE_RENDERER_RASTERIZER_OPENGL_GLOBAL_ILLUMINATION_TEXTURES_HPP

#include "Renderer/GlobalIllumination.hpp"

#include <cstdint>

namespace Renderer::RasterizerOpenGL {

class GlobalIlluminationTextures {
public:
    GlobalIlluminationTextures() = default;
    ~GlobalIlluminationTextures();

    GlobalIlluminationTextures(const GlobalIlluminationTextures&) = delete;
    GlobalIlluminationTextures& operator=(const GlobalIlluminationTextures&) = delete;

    bool bind(const GlobalIllumination::Field *field, int first_texture_unit);
    void clear();

private:
    unsigned int textures_[4]{};
    std::uint64_t revision_ = 0u;
    bool uploaded_ = false;
};

} // namespace Renderer::RasterizerOpenGL

#endif
