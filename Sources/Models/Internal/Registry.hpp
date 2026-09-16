#ifndef HORSE_MODELS_INTERNAL_REGISTRY_HPP
#define HORSE_MODELS_INTERNAL_REGISTRY_HPP

#include "Models/Models.hpp"

namespace Models::Internal {

MeshHandle registerMesh(MeshData mesh);
MaterialHandle registerMaterial(MaterialData material);
bool updateMesh(MeshHandle handle, const MeshData& replacement);
bool updateMaterial(MaterialHandle handle, const MaterialData& replacement);

} // namespace Models::Internal

#endif
