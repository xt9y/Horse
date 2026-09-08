#include "Models/Formats/Fbx.hpp"

#include "Models/Formats/FbxAnimation.hpp"
#include "Models/Formats/FbxDocument.hpp"
#include "Models/Formats/FbxGeometry.hpp"
#include "Models/Formats/FbxMaterial.hpp"
#include "Models/Formats/FbxScene.hpp"
#include "Models/Formats/FbxSkin.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Models::Fbx {

bool load(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) {
        if (error) *error = "null FBX destination";
        return false;
    }
    *document = {};

    FbxDocument::Document raw;
    std::string parse_error;
    if (!FbxDocument::parseFile(path, &raw, &parse_error)) {
        if (error) *error = "cannot parse FBX: " + path + ": " + parse_error;
        return false;
    }

    FbxInternal::Scene scene;
    if (!FbxInternal::buildScene(raw, &scene, error)) return false;

    const std::filesystem::path source_path(path);
    const FbxInternal::SkeletonBuild skeleton = FbxInternal::collectSkeleton(scene);
    document->has_skeleton = !skeleton.models.empty();
    if (document->has_skeleton) {
        if (!FbxInternal::makeSkeleton(
                scene,
                skeleton,
                source_path.stem().string(),
                &document->skeleton,
                error)) {
            return false;
        }
        if (!FbxInternal::makeAnimations(scene, skeleton, &document->animations, error)) return false;
    }

    for (FbxInternal::ObjectId geometry_id : scene.object_order) {
        const FbxInternal::Object *geometry = FbxInternal::object(scene, geometry_id);
        if (!geometry || geometry->kind != "Geometry" || geometry->subtype != "Mesh") continue;

        const auto vertex_values = geometry->node->child("Vertices")
            ? geometry->node->child("Vertices")->numericArray()
            : std::vector<double>{};
        if (vertex_values.empty() || vertex_values.size() % 3u != 0u) {
            if (error) *error = "Geometry `" + geometry->name + "` has malformed Vertices data";
            return false;
        }
        const std::size_t control_count = vertex_values.size() / 3u;

        const FbxInternal::ObjectId model_id = FbxInternal::geometryModel(scene, geometry_id);
        const std::vector<FbxInternal::ObjectId> model_materials =
            FbxInternal::modelMaterials(scene, model_id);

        const FbxInternal::ObjectId skin_id = FbxInternal::skinForGeometry(scene, geometry_id);
        const bool skinned = skin_id != 0;
        if (skinned && !document->has_skeleton) {
            if (error) *error = "Geometry `" + geometry->name + "` has a Skin deformer but no valid skeleton";
            return false;
        }

        std::vector<Animation::SkinWeights> control_weights;
        std::vector<Animation::Mat4> bind_palette;
        if (skinned) {
            if (!FbxInternal::buildControlWeights(
                    scene,
                    skin_id,
                    skeleton,
                    control_count,
                    &control_weights,
                    error)) {
                return false;
            }
            if (!FbxInternal::buildMeshBindPalette(
                    scene,
                    skin_id,
                    skeleton,
                    &bind_palette,
                    error)) {
                return false;
            }
        }

        std::vector<FbxInternal::GeometryPart> converted_parts;
        if (!FbxInternal::convertGeometry(
                scene,
                geometry_id,
                model_materials,
                control_weights,
                bind_palette,
                skinned,
                &converted_parts,
                error)) {
            return false;
        }

        for (FbxInternal::GeometryPart& converted : converted_parts) {
            Part part;
            part.mesh = std::move(converted.mesh);
            if (converted.material != 0) {
                if (!FbxInternal::convertMaterial(
                        scene,
                        converted.material,
                        source_path,
                        &part.material,
                        error)) {
                    return false;
                }
            }
            if (part.material.name.empty()) part.material.name = geometry->name;
            document->parts.push_back(std::move(part));
        }
    }

    if (document->parts.empty()) {
        if (error) *error = "FBX contains no renderable mesh geometry: " + path;
        return false;
    }
    return true;
}

} // namespace Models::Fbx
