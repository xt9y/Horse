#ifndef HORSE_MODELS_IMAGES_WEBP_VP8_HPP
#define HORSE_MODELS_IMAGES_WEBP_VP8_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Images::WebpVp8 {

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);

} // namespace Models::Images::WebpVp8

#endif
