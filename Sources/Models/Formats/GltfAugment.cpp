#include "Models/Formats/GltfAugment.hpp"

#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Models::Formats::GltfAugment {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

bool genericAttribute(
    const GltfData::Context& context,
    int accessor_index,
    AttributeData *out,
    std::string *error)
{
    if (!out || accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= context.accessors.size())
        return fail(error, "glTF generic attribute references invalid accessor");
    const GltfData::Accessor& accessor = context.accessors[static_cast<std::size_t>(accessor_index)];
    out->component_type = accessor.component_type;
    out->components = static_cast<std::uint32_t>(GltfData::componentCount(accessor.type));
    out->normalized = accessor.normalized;
    if (out->components == 0u) return fail(error, "glTF generic attribute has invalid accessor type");

    if (!accessor.normalized &&
        (accessor.component_type == 5121 || accessor.component_type == 5123 || accessor.component_type == 5125)) {
        std::vector<std::uint32_t> values;
        if (!GltfData::decodeUnsigned(context, accessor_index, &values, out->components, error)) return false;
        out->values.reserve(values.size());
        for (const std::uint32_t value : values) out->values.push_back(static_cast<double>(value));
        return true;
    }

    std::vector<float> values;
    if (!GltfData::decodeFloats(context, accessor_index, &values, out->components, error)) return false;
    out->values.reserve(values.size());
    for (const float value : values) out->values.push_back(static_cast<double>(value));
    return true;
}

bool retainAttributes(
    const GltfData::Context& context,
    const Value *attributes,
    std::unordered_map<std::string, AttributeData> *out,
    std::string *error)
{
    if (!attributes) return true;
    if (!attributes->is(Type::Object) || !out) return fail(error, "glTF attributes must be an object");
    for (const auto& [semantic, accessor_value] : attributes->object) {
        const int accessor = GltfJson::integer(&accessor_value);
        AttributeData data;
        if (!genericAttribute(context, accessor, &data, error)) return false;
        out->insert_or_assign(semantic, std::move(data));
    }
    return true;
}

struct PrimitiveRef {
    std::size_t mesh = 0u;
    std::size_t primitive = 0u;
    std::uint32_t node = INVALID_INDEX;
};

bool primitiveOrder(const Value& root, std::vector<PrimitiveRef> *out, std::string *error)
{
    if (!out) return false;
    out->clear();
    const Value *meshes = root.get("meshes");
    if (!meshes) return true;
    if (!meshes->is(Type::Array)) return fail(error, "glTF meshes must be an array");
    const Value *nodes = root.get("nodes");
    std::vector<std::uint8_t> referenced(meshes->array.size(), 0u);

    if (nodes) {
        if (!nodes->is(Type::Array)) return fail(error, "glTF nodes must be an array");
        for (std::size_t node_index = 0u; node_index < nodes->array.size(); ++node_index) {
            const Value& node = nodes->array[node_index];
            if (!node.is(Type::Object)) return fail(error, "invalid glTF node object");
            const int mesh_index = GltfJson::integer(node.get("mesh"));
            if (mesh_index < 0) continue;
            if (static_cast<std::size_t>(mesh_index) >= meshes->array.size()) return fail(error, "glTF node references invalid mesh");
            referenced[static_cast<std::size_t>(mesh_index)] = 1u;
            const Value *primitives = meshes->array[static_cast<std::size_t>(mesh_index)].get("primitives");
            if (!primitives || !primitives->is(Type::Array)) return fail(error, "glTF mesh has no primitives array");
            for (std::size_t primitive = 0u; primitive < primitives->array.size(); ++primitive)
                out->push_back({static_cast<std::size_t>(mesh_index), primitive, static_cast<std::uint32_t>(node_index)});
        }
    }

    for (std::size_t mesh_index = 0u; mesh_index < meshes->array.size(); ++mesh_index) {
        if (referenced[mesh_index] != 0u) continue;
        const Value *primitives = meshes->array[mesh_index].get("primitives");
        if (!primitives || !primitives->is(Type::Array)) return fail(error, "glTF mesh has no primitives array");
        for (std::size_t primitive = 0u; primitive < primitives->array.size(); ++primitive)
            out->push_back({mesh_index, primitive, INVALID_INDEX});
    }
    return true;
}

bool augmentParts(
    const Value& root,
    const GltfData::Context& context,
    Document *document,
    std::string *error)
{
    std::vector<PrimitiveRef> refs;
    if (!primitiveOrder(root, &refs, error)) return false;
    if (refs.size() != document->parts.size())
        return fail(error, "glTF primitive mapping no longer matches imported part count");
    const Value *meshes = root.get("meshes");
    if (!meshes) return true;

    for (std::size_t part_index = 0u; part_index < refs.size(); ++part_index) {
        const PrimitiveRef& ref = refs[part_index];
        const Value& mesh_source = meshes->array[ref.mesh];
        const Value *primitives = mesh_source.get("primitives");
        const Value& primitive = primitives->array[ref.primitive];
        Part& part = document->parts[part_index];
        if (!retainAttributes(context, primitive.get("attributes"), &part.mesh.attributes, error)) return false;

        const Value *targets = primitive.get("targets");
        if (targets) {
            if (!targets->is(Type::Array) || targets->array.size() != part.mesh.morph_targets.size())
                return fail(error, "glTF morph target mapping mismatch");
            for (std::size_t target = 0u; target < targets->array.size(); ++target) {
                if (!retainAttributes(
                    context,
                    &targets->array[target],
                    &part.mesh.morph_targets[target].attributes,
                    error
                )) return false;
            }
        }
        if (part.node != ref.node) return fail(error, "glTF node-to-part mapping mismatch");
    }
    return true;
}

bool augmentNodes(const Value& root, Document *document, std::string *error)
{
    const Value *nodes = root.get("nodes");
    if (!nodes) return true;
    if (!nodes->is(Type::Array) || nodes->array.size() != document->nodes.size())
        return fail(error, "glTF node retention mismatch");
    for (std::size_t index = 0u; index < nodes->array.size(); ++index) {
        const int mesh = GltfJson::integer(nodes->array[index].get("mesh"));
        document->nodes[index].mesh = mesh < 0 ? INVALID_INDEX : static_cast<std::uint32_t>(mesh);
    }
    return true;
}

bool augmentInstances(
    const Value& root,
    const GltfData::Context& context,
    Document *document,
    std::string *error)
{
    const Value *nodes = root.get("nodes");
    if (!nodes || !nodes->is(Type::Array)) return true;
    std::size_t instance_index = 0u;
    for (std::size_t node_index = 0u; node_index < nodes->array.size(); ++node_index) {
        const Value *extensions = nodes->array[node_index].get("extensions");
        const Value *instancing = extensions && extensions->is(Type::Object)
            ? extensions->get("EXT_mesh_gpu_instancing") : nullptr;
        if (!instancing) continue;
        if (instance_index >= document->instances.size()) return fail(error, "glTF instancing retention mismatch");
        InstanceData& instance = document->instances[instance_index++];
        const Value *attributes = instancing->get("attributes");
        if (!attributes || !attributes->is(Type::Object)) return fail(error, "GPU instancing has no attributes object");
        instance.attributes.clear();
        for (const auto& [semantic, accessor_value] : attributes->object) {
            AttributeData data;
            if (!genericAttribute(context, GltfJson::integer(&accessor_value), &data, error)) return false;
            instance.attributes.insert_or_assign(semantic, std::move(data));
        }
    }
    if (instance_index != document->instances.size()) return fail(error, "glTF instancing count mismatch");
    return true;
}

} // namespace

bool apply(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) return fail(error, "null glTF document augmentation target");
    std::string json;
    std::vector<std::uint8_t> binary;
    if (!GltfData::parseContainer(path, &json, &binary, error)) return false;
    Value root;
    if (!GltfJson::parse(json, &root, error)) return false;
    if (!root.is(Type::Object)) return fail(error, "glTF root must be an object");

    GltfData::Context context;
    context.path = path;
    context.directory = std::filesystem::path(path).parent_path();
    context.root = &root;
    if (!GltfData::loadBuffers(&context, binary, error) ||
        !GltfData::loadViews(&context, error) ||
        !GltfData::loadAccessors(&context, error))
        return false;

    return augmentNodes(root, document, error) &&
        augmentParts(root, context, document, error) &&
        augmentInstances(root, context, document, error);
}

} // namespace Models::Formats::GltfAugment
