#ifndef HORSE_MODELS_IMAGES_WEBP_LOSSLESS_HPP
#define HORSE_MODELS_IMAGES_WEBP_LOSSLESS_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images::WebpLossless {

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);

bool decodeStream(
    const std::uint8_t *data,
    std::size_t size,
    int width,
    int height,
    std::vector<std::uint32_t> *argb,
    std::string *error = nullptr
);

} // namespace Models::Images::WebpLossless

#endif
