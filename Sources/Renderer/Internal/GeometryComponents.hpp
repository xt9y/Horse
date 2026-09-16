#ifndef HORSE_RENDERER_INTERNAL_GEOMETRY_COMPONENTS_HPP
#define HORSE_RENDERER_INTERNAL_GEOMETRY_COMPONENTS_HPP

#include <array>
#include <cstdint>
#include <vector>

namespace Renderer::Internal {

struct MeshComponent {
    std::uint32_t mesh = UINT32_MAX;
    std::uint32_t material = UINT32_MAX;
};

struct InstanceComponent {
    std::vector<std::array<float, 16>> matrices;
};

struct RenderableComponent {
    bool visible = false;
};

} // namespace Renderer::Internal

#endif
