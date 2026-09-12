#ifndef HORSE_MODELS_FORMATS_GLTF_MATERIAL_SOURCES_HPP
#define HORSE_MODELS_FORMATS_GLTF_MATERIAL_SOURCES_HPP

#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats::GltfMaterialSources {

bool apply(const std::string& path, Document *output, std::string *error = nullptr);

} // namespace Models::Formats::GltfMaterialSources

#endif
