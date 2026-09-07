#ifndef RW_ENGINE_MODELS_IMAGES_TGA_HPP
#define RW_ENGINE_MODELS_IMAGES_TGA_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::Tga {

using Image = Images::Image;

bool matches(const std::uint8_t *data, std::size_t size);
bool decode(
    const std::uint8_t *data,
    std::size_t size,
    Image *image,
    std::string *error = nullptr
);
bool load(const std::string& path, Image *image, std::string *error = nullptr);

} // namespace Models::Tga

#endif
