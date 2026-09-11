#ifndef HORSE_MODELS_FORMATS_GLTF_HPP
#define HORSE_MODELS_FORMATS_GLTF_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::Gltf {

namespace Formats = ::Models::Formats;

bool load(const std::string& path, Formats::Document *output, std::string *error = nullptr);

} // namespace Models::Formats::Gltf

#endif
