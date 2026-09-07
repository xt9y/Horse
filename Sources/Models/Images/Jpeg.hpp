#ifndef RW_ENGINE_MODELS_IMAGES_JPEG_HPP
#define RW_ENGINE_MODELS_IMAGES_JPEG_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Images::Jpeg {

bool matches(const std::uint8_t *data, std::size_t size);

bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);

} // namespace Models::Images::Jpeg

#endif
