#ifndef HORSE_MODELS_FORMATS_GLTF_DRACO_HPP
#define HORSE_MODELS_FORMATS_GLTF_DRACO_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfDraco {

bool load(const std::string& path, Document *output, std::string *error = nullptr);

} // namespace Models::Formats::GltfDraco

#endif
