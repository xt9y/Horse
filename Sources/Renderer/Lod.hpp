#ifndef HORSE_RENDERER_LOD_HPP
#define HORSE_RENDERER_LOD_HPP

#include <cstdint>
#include <vector>

namespace Renderer {

struct LodLevel {
    float minimum_distance = 0.0f;
    std::uint32_t mesh = UINT32_MAX;
    std::uint32_t material = UINT32_MAX;
};

struct LodGroup {
    std::vector<LodLevel> levels;
};

} // namespace Renderer

#endif
