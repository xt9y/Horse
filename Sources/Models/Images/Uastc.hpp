#ifndef HORSE_MODELS_IMAGES_UASTC_HPP
#define HORSE_MODELS_IMAGES_UASTC_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Images::Uastc {

bool decodeBlock(
    const std::uint8_t *block,
    std::uint8_t rgba[64],
    std::string *error = nullptr
);

bool decodeImage(
    const std::uint8_t *blocks,
    std::size_t size,
    int width,
    int height,
    Image *image,
    std::string *error = nullptr
);

} // namespace Models::Images::Uastc

#endif
