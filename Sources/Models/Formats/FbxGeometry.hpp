#ifndef RW_ENGINE_MODELS_FORMATS_FBX_GEOMETRY_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_GEOMETRY_HPP

#include "Models/Formats/FbxTransform.hpp"
#include "Models/Models.hpp"

#include <string>
#include <vector>

namespace Models::FbxInternal {

struct GeometryPart {
    MeshData mesh;
    ObjectId material = 0;
};

bool convertGeometry(
    const Scene& scene,
    ObjectId geometry_id,
    const std::vector<ObjectId>& model_materials,
    const std::vector<Animation::SkinWeights>& control_weights,
    const std::vector<Animation::Mat4>& bind_palette,
    bool skinned,
    std::vector<GeometryPart> *out,
    std::string *error);

} // namespace Models::FbxInternal

#endif
