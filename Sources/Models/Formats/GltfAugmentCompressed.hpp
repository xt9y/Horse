#ifndef HORSE_MODELS_FORMATS_GLTF_AUGMENT_COMPRESSED_HPP
#define HORSE_MODELS_FORMATS_GLTF_AUGMENT_COMPRESSED_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfAugmentCompressed {

bool apply(const std::string& path, Document *document, std::string *error = nullptr);

} // namespace Models::Formats::GltfAugmentCompressed

#endif
