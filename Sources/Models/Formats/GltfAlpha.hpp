#ifndef HORSE_MODELS_FORMATS_GLTF_ALPHA_HPP
#define HORSE_MODELS_FORMATS_GLTF_ALPHA_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfAlpha {

bool apply(Document *document, std::string *error = nullptr);

} // namespace Models::Formats::GltfAlpha

#endif
