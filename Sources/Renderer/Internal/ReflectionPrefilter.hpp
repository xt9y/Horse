#ifndef HORSE_RENDERER_INTERNAL_REFLECTION_PREFILTER_HPP
#define HORSE_RENDERER_INTERNAL_REFLECTION_PREFILTER_HPP

#include "Models/Images/Image.hpp"
#include "Renderer/Quality.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace Renderer::Reflections::Internal {

struct PrefilterLevel {
    std::uint32_t width = 1u;
    std::uint32_t height = 1u;
    std::vector<std::uint8_t> rgba;
};

struct PrefilterChain {
    std::vector<PrefilterLevel> levels;
};

struct IrradianceSH {
    std::array<float, 3> average{};
    std::array<float, 3> x{};
    std::array<float, 3> y{};
    std::array<float, 3> z{};
};

std::uint32_t prefilterSampleCount(Quality quality);

IrradianceSH irradianceEquirectangular(const Models::Images::Image& source);

PrefilterChain prefilterEquirectangular(
    const Models::Images::Image& source,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_levels,
    Quality quality
);

} // namespace Renderer::Reflections::Internal

#endif
