#ifndef HORSE_RENDERER_QUALITY_HPP
#define HORSE_RENDERER_QUALITY_HPP

#include <cstdint>

namespace Renderer {

enum class Quality : std::uint8_t {
    Low,
    Medium,
    High,
    Ultra,
};

} // namespace Renderer

#endif
