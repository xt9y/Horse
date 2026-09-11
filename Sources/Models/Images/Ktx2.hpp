#ifndef HORSE_MODELS_IMAGES_KTX2_HPP
#define HORSE_MODELS_IMAGES_KTX2_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Images::Ktx2 {

bool matches(const std::uint8_t *data, std::size_t size);
bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);

} // namespace Models::Images::Ktx2

#endif
