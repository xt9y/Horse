#include "Models/Formats/GltfAugmentCompressed.hpp"

#include "Models/Compression/Draco.hpp"
#include "Models/Formats/GltfAugment.hpp"
#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Models::Formats::GltfAugmentCompressed {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

const Value *dracoExtension(const Value& primitive)
{
    const Value *extensions = primitive.get("extensions");
    return extensions && extensions->is(Type::Object)
        ? extensions->get("KHR_draco_mesh_compression")
        : nullptr;
}

bool hasDraco(const Value& root)
{
    const Value *meshes = root.get("meshes");
    if (!meshes || !meshes->is(Type::Array)) return false;
    for (const Value& mesh : meshes->array) {
        const Value *primitives = mesh.get("primitives");
        if (!primitives || !primitives->is(Type::Array)) continue;
        for (const Value& primitive : primitives->array)
            if (dracoExtension(primitive)) return true;
    }
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
        for (std::uint32_t value : values) out->values.push_back(static_cast<double>(value));
        return true;
    }

    std::vector<float> values;
    if (!GltfData::decodeFloats(context, accessor_index, &values, out->components, error)) return false;
    out->values.reserve(values.size());
    for (float value : values) out->values.push_back(static_cast<double>(value));
    return true;
}

bool rawView(
    const GltfData::Context& context,
    int index,
    const std::uint8_t **data,
    std::size_t *size,
    std::string *error)
{
    if (!data || !size || index < 0 || static_cast<std::size_t>(index) >= context.views.size())
        return fail(error, "KHR_draco_mesh_compression references invalid bufferView");
    const GltfData::BufferView& view = context.views[static_cast<std::size_t>(index)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= context.buffers.size())
        return fail(error, "Draco bufferView references invalid buffer");
    const std::vector<std::uint8_t>& buffer = context.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.offset > buffer.size() || view.length > buffer.size() - view.offset)
        return fail(error, "Draco bufferView exceeds buffer bounds");
    *data = buffer.data() + view.offset;
    *size = view.length;
    return true;
}

double normalizedInteger(double value, int type)
{
    switch (type) {
        case 5120: return std::max(-1.0, value / 127.0);
        case 5121: return value / 255.0;
        case 5122: return std::max(-1.0, value / 32767.0);
        case 5123: return value / 65535.0;
        case 5125: return value / 4294967295.0;
        default: return value;
    }
}

double clampInteger(double value, int type)
{
    value = std::round(value);
    switch (type) {
        case 5120: return std::clamp(value, -128.0, 127.0);
        case 5121: return std::clamp(value, 0.0, 255.0);
        case 5122: return std::clamp(value, -32768.0, 32767.0);
        case 5123: return std::clamp(value, 0.0, 65535.0);
        case 5125: return std::clamp(value, 0.0, 4294967295.0);
        default: return value;
    }
}

bool decodedDracoAttribute(
    const Compression::DracoAttribute& source,
    const GltfData::Accessor& accessor,
    AttributeData *out,
    std::string *error)
{
    if (!out) return false;
    const std::size_t components = GltfData::componentCount(accessor.type);
    if (components == 0u || source.components != components)
        return fail(error, "Draco generic attribute component count does not match glTF accessor");
    if (accessor.count > std::numeric_limits<std::size_t>::max() / components ||
        source.values.size() != accessor.count * components)
        return fail(error, "Draco generic attribute value count does not match glTF accessor");

    out->component_type = accessor.component_type;
    out->components = static_cast<std::uint32_t>(components);
    out->normalized = accessor.normalized;
    out->values.clear();
    out->values.reserve(source.values.size());
    const bool source_integral = source.data_type != 9u && source.data_type != 10u;
    for (double value : source.values) {
        if (!std::isfinite(value)) return fail(error, "Draco generic attribute contains a non-finite value");
        if (accessor.normalized) {
            value = source_integral
                ? normalizedInteger(clampInteger(value, accessor.component_type), accessor.component_type)
                : (accessor.component_type == 5120 || accessor.component_type == 5122
                    ? std::clamp(value, -1.0, 1.0)
                    : std::clamp(value, 0.0, 1.0));
        } else if (accessor.component_type != 5126) {
            value = clampInteger(value, accessor.component_type);
        }
        out->values.push_back(value);
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
            if (static_cast<std::size_t>(mesh_index) >= meshes->array.size())
                return fail(error, "glTF node references invalid mesh");
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
        const Value *primitives = meshes->array[ref.mesh].get("primitives");
        if (!primitives || !primitives->is(Type::Array) || ref.primitive >= primitives->array.size())
            return fail(error, "glTF primitive mapping is invalid");
        const Value& primitive = primitives->array[ref.primitive];
        Part& part = document->parts[part_index];
        const Value *primitive_attributes = primitive.get("attributes");
        if (primitive_attributes && !primitive_attributes->is(Type::Object))
            return fail(error, "glTF attributes must be an object");

        const Value *draco = dracoExtension(primitive);
        const Value *draco_attributes = nullptr;
        Compression::DracoMesh decoded;
        if (draco) {
            if (!draco->is(Type::Object)) return fail(error, "KHR_draco_mesh_compression must be an object");
            draco_attributes = draco->get("attributes");
            if (!draco_attributes || !draco_attributes->is(Type::Object))
                return fail(error, "KHR_draco_mesh_compression attributes must be an object");
            const std::uint8_t *bytes = nullptr;
            std::size_t size = 0u;
            if (!rawView(context, GltfJson::integer(draco->get("bufferView")), &bytes, &size, error) ||
                !Compression::decodeDracoAny(bytes, size, &decoded, error))
                return false;
        }

        if (primitive_attributes) {
            for (const auto& [semantic, accessor_value] : primitive_attributes->object) {
                const int accessor_index = GltfJson::integer(&accessor_value);
                if (accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= context.accessors.size())
                    return fail(error, "glTF generic attribute references invalid accessor: " + semantic);
                AttributeData data;
                const Value *unique = draco_attributes ? draco_attributes->get(semantic) : nullptr;
                if (unique) {
                    if (!unique->is(Type::Number)) return fail(error, "Draco attribute id must be numeric: " + semantic);
                    const Compression::DracoAttribute *source = decoded.attribute(GltfJson::unsignedInteger(unique));
                    if (!source) return fail(error, "Draco stream is missing generic attribute: " + semantic);
                    if (!decodedDracoAttribute(
                            *source,
                            context.accessors[static_cast<std::size_t>(accessor_index)],
                            &data,
                            error))
                        return false;
                } else if (!genericAttribute(context, accessor_index, &data, error)) {
                    return false;
                }
                part.mesh.attributes.insert_or_assign(semantic, std::move(data));
            }
        }

        const Value *targets = primitive.get("targets");
        if (targets) {
            if (!targets->is(Type::Array) || targets->array.size() != part.mesh.morph_targets.size())
                return fail(error, "glTF morph target mapping mismatch");
            for (std::size_t target = 0u; target < targets->array.size(); ++target) {
                if (!targets->array[target].is(Type::Object)) return fail(error, "glTF morph target must be an object");
                for (const auto& [semantic, accessor_value] : targets->array[target].object) {
                    AttributeData data;
                    if (!genericAttribute(context, GltfJson::integer(&accessor_value), &data, error)) return false;
                    part.mesh.morph_targets[target].attributes.insert_or_assign(semantic, std::move(data));
                }
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
    return instance_index == document->instances.size()
        ? true : fail(error, "glTF instancing count mismatch");
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
    if (!GltfJson::parse(json, &root, error) || !root.is(Type::Object))
        return fail(error, error && !error->empty() ? *error : "glTF root must be an object");

    if (!hasDraco(root)) return GltfAugment::apply(path, document, error);

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

} // namespace Models::Formats::GltfAugmentCompressed
