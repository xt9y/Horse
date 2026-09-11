#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_RECONSTRUCT_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_RECONSTRUCT_HPP

#include "Models/Images/Image.hpp"
#include "Models/Images/WebpVp8Frame.hpp"
#include "Models/Images/WebpVp8Modes.hpp"
#include "Models/Images/WebpVp8Residue.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images::WebpVp8Internal {

struct Plane {
    std::size_t width = 0u;
    std::size_t height = 0u;
    std::vector<std::uint8_t> pixels;

    std::uint8_t& at(std::size_t x, std::size_t y) { return pixels[y * width + x]; }
    std::uint8_t at(std::size_t x, std::size_t y) const { return pixels[y * width + x]; }
};

struct YuvFrame {
    int width = 0;
    int height = 0;
    Plane y;
    Plane u;
    Plane v;
};

bool reconstruct(
    const KeyFrame& frame,
    const MacroblockGrid& modes,
    const ResidueGrid& residue,
    YuvFrame *output,
    std::string *error = nullptr
);

bool toRgba(const YuvFrame& frame, Image *image, std::string *error = nullptr);

} // namespace Models::Images::WebpVp8Internal

#endif
