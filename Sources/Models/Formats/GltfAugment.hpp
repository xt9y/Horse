#ifndef HORSE_MODELS_FORMATS_GLTF_AUGMENT_HPP
#define HORSE_MODELS_FORMATS_GLTF_AUGMENT_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfAugment {

bool apply(const std::string& path, Document *document, std::string *error = nullptr);

} // namespace Models::Formats::GltfAugment

#endif
