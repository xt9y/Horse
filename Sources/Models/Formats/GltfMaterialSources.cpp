#include "Models/Formats/GltfMaterialSources.hpp"

#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Models::Formats::GltfMaterialSources {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool appendMesh(
    const Value& root,
    int mesh_index,
    std::vector<std::uint32_t> *part_sources,
    std::vector<std::uint32_t> *variant_sources,
    std::string *error)
{
    const Value *meshes = root.get("meshes");
    if (!meshes || !meshes->is(Type::Array) || mesh_index < 0 ||
        static_cast<std::size_t>(mesh_index) >= meshes->array.size())
        return fail(error, "glTF source material augmentation references invalid mesh");

    const Value& mesh = meshes->array[static_cast<std::size_t>(mesh_index)];
    const Value *primitives = mesh.get("primitives");
    if (!primitives || !primitives->is(Type::Array))
        return fail(error, "glTF source material augmentation found mesh without primitives");

    for (const Value& primitive : primitives->array) {
        const int material = GltfJson::integer(primitive.get("material"));
        part_sources->push_back(material >= 0 ? static_cast<std::uint32_t>(material) : INVALID_INDEX);

        const Value *extensions = primitive.get("extensions");
        const Value *variants = extensions && extensions->is(Type::Object)
            ? extensions->get("KHR_materials_variants") : nullptr;
        const Value *mappings = variants && variants->is(Type::Object)
            ? variants->get("mappings") : nullptr;
        if (!mappings) continue;
        if (!mappings->is(Type::Array))
            return fail(error, "KHR_materials_variants mappings must be an array");
        for (const Value& mapping : mappings->array) {
            if (!mapping.is(Type::Object))
                return fail(error, "invalid KHR_materials_variants mapping");
            const int mapped_material = GltfJson::integer(mapping.get("material"));
            if (mapped_material < 0)
                return fail(error, "KHR_materials_variants mapping has invalid material");
            variant_sources->push_back(static_cast<std::uint32_t>(mapped_material));
        }
    }
    return true;
}

} // namespace

bool apply(const std::string& path, Document *output, std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error, "null glTF source material output");
    if (output->parts.empty() && output->variant_materials.empty()) return true;

    std::string json;
    std::vector<std::uint8_t> binary;
    if (!GltfData::parseContainer(path, &json, &binary, error)) return false;

    Value root;
    if (!GltfJson::parse(json, &root, error)) return false;
    if (!root.is(Type::Object)) return fail(error, "glTF root must be an object");

    const Value *meshes = root.get("meshes");
    if (!meshes || !meshes->is(Type::Array)) {
        if (output->parts.empty()) return true;
        return fail(error, "glTF source material augmentation found no meshes");
    }

    std::vector<std::uint8_t> referenced(meshes->array.size(), 0u);
    std::vector<std::uint32_t> part_sources;
    std::vector<std::uint32_t> variant_sources;
    part_sources.reserve(output->parts.size());
    variant_sources.reserve(output->variant_materials.size());

    const Value *nodes = root.get("nodes");
    if (nodes) {
        if (!nodes->is(Type::Array)) return fail(error, "glTF nodes must be an array");
        for (const Value& node : nodes->array) {
            if (!node.is(Type::Object)) return fail(error, "invalid glTF node object");
            const int mesh = GltfJson::integer(node.get("mesh"));
            if (mesh < 0) continue;
            if (static_cast<std::size_t>(mesh) >= referenced.size())
                return fail(error, "glTF node references invalid mesh during material augmentation");
            referenced[static_cast<std::size_t>(mesh)] = 1u;
            if (!appendMesh(root, mesh, &part_sources, &variant_sources, error)) return false;
        }
    }

    for (std::size_t mesh = 0u; mesh < meshes->array.size(); ++mesh) {
        if (referenced[mesh] != 0u) continue;
        if (!appendMesh(root, static_cast<int>(mesh), &part_sources, &variant_sources, error)) return false;
    }

    if (part_sources.size() != output->parts.size())
        return fail(error, "glTF source material part order does not match loaded model");
    if (variant_sources.size() != output->variant_materials.size())
        return fail(error, "glTF source material variant order does not match loaded model");

    for (std::size_t index = 0u; index < output->parts.size(); ++index)
        output->parts[index].source_material = part_sources[index];
    for (std::size_t index = 0u; index < output->variant_materials.size(); ++index)
        output->variant_materials[index].source_material = variant_sources[index];
    return true;
}

} // namespace Models::Formats::GltfMaterialSources
