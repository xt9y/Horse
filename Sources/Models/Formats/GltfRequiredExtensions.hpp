#ifndef HORSE_MODELS_FORMATS_GLTF_REQUIRED_EXTENSIONS_HPP
#define HORSE_MODELS_FORMATS_GLTF_REQUIRED_EXTENSIONS_HPP

#include "Models/Formats/Registry.hpp"

namespace Models::Formats::GltfRequiredExtensions {

bool load(const std::string& path, Document *output, std::string *error = nullptr);

} // namespace Models::Formats::GltfRequiredExtensions

#endif
