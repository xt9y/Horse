#ifndef RW_ENGINE_MODELS_FORMATS_FBX_MATERIAL_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_MATERIAL_HPP

#include "Models/Formats/FbxScene.hpp"
#include "Models/Core/Material.hpp"

#include <filesystem>
#include <string>

namespace Models::FbxInternal {

bool convertMaterial(
    const Scene& scene,
    ObjectId material_id,
    const std::filesystem::path& source_path,
    MaterialData *out,
    std::string *error);

} // namespace Models::FbxInternal

#endif
