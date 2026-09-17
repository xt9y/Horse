#ifndef HORSE_MODELS_FORMATS_GLTF_DEPENDENCIES_HPP
#define HORSE_MODELS_FORMATS_GLTF_DEPENDENCIES_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfDependencies {

bool apply(const std::string& path, Document *document, std::string *error = nullptr);

} // namespace Models::Formats::GltfDependencies

#endif
