#ifndef HORSE_MODELS_IMAGES_WEBP_ALPHA_HPP
#define HORSE_MODELS_IMAGES_WEBP_ALPHA_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Images::WebpAlpha {

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    int width,
    int height,
    std::vector<std::uint8_t> *alpha,
    std::string *error = nullptr
);

} // namespace Models::Images::WebpAlpha

#endif
