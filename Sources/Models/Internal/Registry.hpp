#ifndef HORSE_MODELS_INTERNAL_REGISTRY_HPP
#define HORSE_MODELS_INTERNAL_REGISTRY_HPP

#include "Models/Models.hpp"

#include <string>

namespace Models::Formats {
struct Document;
}

namespace Models::Internal {

MeshHandle registerMesh(MeshData mesh);
MaterialHandle registerMaterial(MaterialData material);
bool updateMesh(MeshHandle handle, const MeshData& replacement);
bool updateMaterial(MaterialHandle handle, const MaterialData& replacement);

std::string normalizeModelPath(const std::string& path);
ModelHandle loadedModelForPath(const std::string& normalized_path);
ModelHandle publishDocument(
    const std::string& normalized_path,
    Formats::Document document,
    std::string *error = nullptr
);

} // namespace Models::Internal

#endif
