#ifndef HORSE_MODELS_FORMATS_GLTF_FULL_HPP
#define HORSE_MODELS_FORMATS_GLTF_FULL_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfFull {

bool load(const std::string& path, Document *output, std::string *error = nullptr);

} // namespace Models::Formats::GltfFull

#endif
