#ifndef HORSE_MODELS_IMAGES_ETC1S_HPP
#define HORSE_MODELS_IMAGES_ETC1S_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Images::Etc1s {

bool decodeKtx2BaseLevel(
    const std::uint8_t *global_data,
    std::size_t global_size,
    std::size_t image_count,
    const std::uint8_t *level_data,
    std::size_t level_size,
    int width,
    int height,
    Image *image,
    std::string *error = nullptr
);

} // namespace Models::Images::Etc1s

#endif
