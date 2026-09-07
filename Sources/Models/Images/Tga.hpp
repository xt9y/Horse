#ifndef RW_ENGINE_MODELS_IMAGES_TGA_HPP
#define RW_ENGINE_MODELS_IMAGES_TGA_HPP

#include "Models/Images/Image.hpp"

#include <string>

namespace Models::Tga {

using Image = Images::Image;

bool load(const std::string& path, Image *image, std::string *error = nullptr);

} // namespace Models::Tga

#endif
