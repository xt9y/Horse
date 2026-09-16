#ifndef HORSE_MODELS_INTERNAL_MESH_REVISION_HPP
#define HORSE_MODELS_INTERNAL_MESH_REVISION_HPP

#include "Models/Models.hpp"

#include <cstdint>

namespace Models::Internal {

std::uint64_t meshRevision(MeshHandle handle);
std::uint64_t meshTopologyRevision(MeshHandle handle);

} // namespace Models::Internal

#endif
