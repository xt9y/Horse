#include "Models/Formats/GltfFull.hpp"

#include "Models/Formats/GltfAssets.hpp"
#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Models::Formats::GltfFull {
namespace {

using GltfJson::Type;
using GltfJson::Value;

struct Context {
    Value root;
    GltfAssets::Context assets;
};

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

void preserveObject(
    const Value *source,
    std::string *extras,
    std::unordered_map<std::string, std::string> *extensions)
{
    if (!source || !source->is(Type::Object)) return;
    if (extras) {
        if (const Value *value = source->get("extras")) *extras = GltfJson::stringify(*value);
    }
    if (!extensions) return;
    const Value *value = source->get("extensions");
    if (!value || !value->is(Type::Object)) return;
    for (const auto& [name, extension] : value->object)
        extensions->insert_or_assign(name, GltfJson::stringify(extension));
}

bool supportedRequiredExtension(std::string_view name)
{
    static const std::unordered_set<std::string> supported {
        "KHR_mesh_quantization",
        "KHR_texture_transform",
        "KHR_materials_unlit",
        "KHR_materials_emissive_strength",
        "KHR_materials_ior",
        "KHR_materials_clearcoat",
        "KHR_materials_sheen",
        "KHR_materials_specular",
        "KHR_materials_transmission",
        "KHR_materials_volume",
        "KHR_materials_iridescence",
        "KHR_materials_anisotropy",
        "KHR_materials_dispersion",
        "KHR_materials_diffuse_transmission",
        "KHR_materials_pbrSpecularGlossiness",
        "KHR_lights_punctual",
        "KHR_materials_variants",
        "EXT_mesh_gpu_instancing",
        "KHR_animation_pointer",
        "KHR_node_visibility",
        "KHR_node_selectability",
        "KHR_node_hoverability",
        "EXT_lights_ies",
        "EXT_lights_image_based",
        "EXT_mesh_manifold",
        "MSFT_lod",
    };
    return supported.contains(std::string(name));
}

bool validateRequiredExtensions(const Value& root, std::string *error)
{
    const Value *extensions = root.get("extensionsRequired");
    if (!extensions) return true;
    if (!extensions->is(Type::Array)) return fail(error, "glTF extensionsRequired must be an array");
    for (const Value& extension : extensions->array) {
        if (!extension.is(Type::String)) return fail(error, "glTF extensionsRequired contains a non-string value");
        if (!supportedRequiredExtension(extension.string))
            return fail(error, "required glTF extension is not implemented yet: " + extension.string);
    }
    return true;
}

std::size_t accessorComponents(const Context& context, int accessor)
{
    if (accessor < 0 || static_cast<std::size_t>(accessor) >= context.assets.data.accessors.size()) return 0u;
    return GltfData::componentCount(context.assets.data.accessors[static_cast<std::size_t>(accessor)].type);
}

bool semanticIndex(std::string_view semantic, std::string_view prefix, std::size_t *out)
{
    if (!out || !semantic.starts_with(prefix)) return false;
    if (semantic.size() <= prefix.size() || semantic[prefix.size()] != '_') return false;
    std::size_t value = 0u;
    for (std::size_t i = prefix.size() + 1u; i < semantic.size(); ++i) {
        const char c = semantic[i];
        if (c < '0' || c > '9') return false;
        if (value > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(c - '0')) / 10u)
            return false;
        value = value * 10u + static_cast<std::size_t>(c - '0');
    }
    *out = value;
    return true;
}

void bounds(MeshData *mesh)
{
    if (!mesh || mesh->vertices.empty()) return;
    mesh->bounds.minimum = mesh->vertices.front().position;
    mesh->bounds.maximum = mesh->vertices.front().position;
    for (const Vertex& vertex : mesh->vertices) {
        mesh->bounds.minimum.x = std::min(mesh->bounds.minimum.x, vertex.position.x);
        mesh->bounds.minimum.y = std::min(mesh->bounds.minimum.y, vertex.position.y);
        mesh->bounds.minimum.z = std::min(mesh->bounds.minimum.z, vertex.position.z);
        mesh->bounds.maximum.x = std::max(mesh->bounds.maximum.x, vertex.position.x);
        mesh->bounds.maximum.y = std::max(mesh->bounds.maximum.y, vertex.position.y);
        mesh->bounds.maximum.z = std::max(mesh->bounds.maximum.z, vertex.position.z);
    }
}

void generatedNormals(MeshData *mesh)
{
    if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) return;
    for (Vertex& vertex : mesh->vertices) vertex.normal = {};
    for (std::size_t i = 0u; i + 2u < mesh->indices.size(); i += 3u) {
        const std::uint32_t ia = mesh->indices[i + 0u];
        const std::uint32_t ib = mesh->indices[i + 1u];
        const std::uint32_t ic = mesh->indices[i + 2u];
        if (ia >= mesh->vertices.size() || ib >= mesh->vertices.size() || ic >= mesh->vertices.size()) continue;
        const Vec3 a = mesh->vertices[ia].position;
        const Vec3 b = mesh->vertices[ib].position;
        const Vec3 c = mesh->vertices[ic].position;
        const Vec3 ab {b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 ac {c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 n {
            ab.y * ac.z - ab.z * ac.y,
            ab.z * ac.x - ab.x * ac.z,
            ab.x * ac.y - ab.y * ac.x,
        };
        for (const std::uint32_t index : {ia, ib, ic}) {
            Vertex& vertex = mesh->vertices[index];
            vertex.normal.x += n.x;
            vertex.normal.y += n.y;
            vertex.normal.z += n.z;
        }
    }
    for (Vertex& vertex : mesh->vertices) {
        const float length = std::sqrt(
            vertex.normal.x * vertex.normal.x +
            vertex.normal.y * vertex.normal.y +
            vertex.normal.z * vertex.normal.z
        );
        if (length <= 1.0e-12f) {
            vertex.normal = {0.0f, 1.0f, 0.0f};
        } else {
            vertex.normal.x /= length;
            vertex.normal.y /= length;
            vertex.normal.z /= length;
        }
    }
}

PrimitiveMode primitiveMode(int value)
{
    switch (value) {
        case 0: return PrimitiveMode::Points;
        case 1: return PrimitiveMode::Lines;
        case 2: return PrimitiveMode::LineLoop;
        case 3: return PrimitiveMode::LineStrip;
        case 5: return PrimitiveMode::TriangleStrip;
        case 6: return PrimitiveMode::TriangleFan;
        case 4:
        default: return PrimitiveMode::Triangles;
    }
}

void triangleIndices(PrimitiveMode mode, const std::vector<std::uint32_t>& source, std::vector<std::uint32_t> *out)
{
    if (!out) return;
    out->clear();
    if (mode == PrimitiveMode::Triangles) {
        *out = source;
        return;
    }
    if (mode == PrimitiveMode::TriangleStrip) {
        if (source.size() < 3u) return;
        out->reserve((source.size() - 2u) * 3u);
        for (std::size_t i = 2u; i < source.size(); ++i) {
            std::uint32_t a = source[i - 2u];
            std::uint32_t b = source[i - 1u];
            const std::uint32_t c = source[i];
            if ((i & 1u) != 0u) std::swap(a, b);
            if (a == b || b == c || a == c) continue;
            out->push_back(a);
            out->push_back(b);
            out->push_back(c);
        }
        return;
    }
    if (mode == PrimitiveMode::TriangleFan) {
        if (source.size() < 3u) return;
        out->reserve((source.size() - 2u) * 3u);
        for (std::size_t i = 2u; i < source.size(); ++i) {
            const std::uint32_t a = source[0];
            const std::uint32_t b = source[i - 1u];
            const std::uint32_t c = source[i];
            if (a == b || b == c || a == c) continue;
            out->push_back(a);
            out->push_back(b);
            out->push_back(c);
        }
    }
}

bool decodeVec2Set(Context& context, int accessor, std::size_t count, std::vector<Vec2> *out, std::string *error)
{
    std::vector<float> values;
    if (!GltfData::decodeFloats(context.assets.data, accessor, &values, 2u, error)) return false;
    if (values.size() / 2u != count) return fail(error, "glTF attribute count mismatch");
    out->resize(count);
    for (std::size_t i = 0u; i < count; ++i) (*out)[i] = {values[i * 2u], values[i * 2u + 1u]};
    return true;
}

bool decodeVec4Set(Context& context, int accessor, std::size_t count, std::vector<Vec4> *out, std::string *error)
{
    const std::size_t components = accessorComponents(context, accessor);
    if (components != 3u && components != 4u) return fail(error, "glTF COLOR attribute must be VEC3 or VEC4");
    std::vector<float> values;
    if (!GltfData::decodeFloats(context.assets.data, accessor, &values, components, error)) return false;
    if (values.size() / components != count) return fail(error, "glTF color count mismatch");
    out->resize(count);
    for (std::size_t i = 0u; i < count; ++i) {
        (*out)[i] = {
            values[i * components + 0u],
            values[i * components + 1u],
            values[i * components + 2u],
            components == 4u ? values[i * components + 3u] : 1.0f,
        };
    }
    return true;
}

bool decodeJointSet(Context& context, int accessor, std::size_t count, std::vector<Joint4> *out, std::string *error)
{
    std::vector<std::uint32_t> values;
    if (!GltfData::decodeUnsigned(context.assets.data, accessor, &values, 4u, error)) return false;
    if (values.size() / 4u != count) return fail(error, "glTF JOINTS attribute count mismatch");
    out->resize(count);
    for (std::size_t i = 0u; i < count; ++i) {
        for (std::size_t component = 0u; component < 4u; ++component) {
            if (values[i * 4u + component] > std::numeric_limits<std::uint16_t>::max())
                return fail(error, "glTF joint index exceeds Horse joint storage");
        }
        (*out)[i] = {
            static_cast<std::uint16_t>(values[i * 4u + 0u]),
            static_cast<std::uint16_t>(values[i * 4u + 1u]),
            static_cast<std::uint16_t>(values[i * 4u + 2u]),
            static_cast<std::uint16_t>(values[i * 4u + 3u]),
        };
    }
    return true;
}

bool decodeWeightSet(Context& context, int accessor, std::size_t count, std::vector<Vec4> *out, std::string *error)
{
    std::vector<float> values;
    if (!GltfData::decodeFloats(context.assets.data, accessor, &values, 4u, error)) return false;
    if (values.size() / 4u != count) return fail(error, "glTF WEIGHTS attribute count mismatch");
    out->resize(count);
    for (std::size_t i = 0u; i < count; ++i)
        (*out)[i] = {values[i * 4u], values[i * 4u + 1u], values[i * 4u + 2u], values[i * 4u + 3u]};
    return true;
}

bool morphVector(Context& context, const Value *accessor_value, std::size_t count, std::vector<Vec3> *out, std::string *error)
{
    if (!accessor_value) return true;
    const int accessor = GltfJson::integer(accessor_value);
    std::vector<float> values;
    if (!GltfData::decodeFloats(context.assets.data, accessor, &values, 3u, error)) return false;
    if (values.size() / 3u != count) return fail(error, "glTF morph target count mismatch");
    out->resize(count);
    for (std::size_t i = 0u; i < count; ++i)
        (*out)[i] = {values[i * 3u], values[i * 3u + 1u], values[i * 3u + 2u]};
    return true;
}

bool parseMorphTargets(Context& context, const Value *targets, std::size_t count, MeshData *mesh, std::string *error)
{
    if (!targets) return true;
    if (!targets->is(Type::Array)) return fail(error, "glTF primitive targets must be an array");
    mesh->morph_targets.reserve(targets->array.size());
    for (const Value& source : targets->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF morph target");
        MorphTargetData target;
        if (!morphVector(context, source.get("POSITION"), count, &target.positions, error) ||
            !morphVector(context, source.get("NORMAL"), count, &target.normals, error) ||
            !morphVector(context, source.get("TANGENT"), count, &target.tangents, error))
            return false;
        mesh->morph_targets.push_back(std::move(target));
    }
    return true;
}

void meshWeightsAndNames(const Value& mesh_source, MeshData *mesh)
{
    if (!mesh) return;
    const Value *weights = mesh_source.get("weights");
    if (weights && weights->is(Type::Array)) {
        mesh->morph_weights.reserve(weights->array.size());
        for (const Value& value : weights->array)
            mesh->morph_weights.push_back(GltfJson::floatValue(&value));
    }
    const Value *extras = mesh_source.get("extras");
    if (!extras || !extras->is(Type::Object)) return;
    const Value *names = extras->get("targetNames");
    if (!names || !names->is(Type::Array)) return;
    mesh->morph_names.reserve(names->array.size());
    for (const Value& name : names->array)
        mesh->morph_names.push_back(GltfJson::stringValue(&name));
}

bool primitiveMesh(
    Context& context,
    const Value& mesh_source,
    const Value& primitive,
    MeshData *out,
    std::string *error)
{
    if (!out || !primitive.is(Type::Object)) return fail(error, "invalid glTF primitive");
    const Value *attributes = primitive.get("attributes");
    if (!attributes || !attributes->is(Type::Object)) return fail(error, "glTF primitive has no attributes");
    const int position_accessor = GltfJson::integer(attributes->get("POSITION"));
    if (position_accessor < 0) return fail(error, "glTF primitive has no POSITION accessor");

    std::vector<float> positions;
    if (!GltfData::decodeFloats(context.assets.data, position_accessor, &positions, 3u, error)) return false;
    const std::size_t vertex_count = positions.size() / 3u;
    if (vertex_count == 0u) return fail(error, "glTF primitive POSITION accessor is empty");

    MeshData mesh;
    mesh.vertices.resize(vertex_count);
    for (std::size_t i = 0u; i < vertex_count; ++i)
        mesh.vertices[i].position = {positions[i * 3u], positions[i * 3u + 1u], positions[i * 3u + 2u]};

    bool has_normals = false;
    if (const Value *normal_value = attributes->get("NORMAL")) {
        std::vector<float> normals;
        if (!GltfData::decodeFloats(context.assets.data, GltfJson::integer(normal_value), &normals, 3u, error)) return false;
        if (normals.size() / 3u != vertex_count) return fail(error, "glTF NORMAL count mismatch");
        for (std::size_t i = 0u; i < vertex_count; ++i)
            mesh.vertices[i].normal = {normals[i * 3u], normals[i * 3u + 1u], normals[i * 3u + 2u]};
        has_normals = true;
    }

    if (const Value *tangent_value = attributes->get("TANGENT")) {
        std::vector<float> tangents;
        if (!GltfData::decodeFloats(context.assets.data, GltfJson::integer(tangent_value), &tangents, 4u, error)) return false;
        if (tangents.size() / 4u != vertex_count) return fail(error, "glTF TANGENT count mismatch");
        for (std::size_t i = 0u; i < vertex_count; ++i)
            mesh.vertices[i].tangent = {tangents[i * 4u], tangents[i * 4u + 1u], tangents[i * 4u + 2u], tangents[i * 4u + 3u]};
    }

    std::size_t highest_texcoord = 0u;
    std::size_t highest_color = 0u;
    std::size_t highest_joint = 0u;
    std::size_t highest_weight = 0u;
    bool have_texcoord = false;
    bool have_color = false;
    bool have_joint = false;
    bool have_weight = false;
    for (const auto& [semantic, accessor_value] : attributes->object) {
        std::size_t set = 0u;
        if (semanticIndex(semantic, "TEXCOORD", &set)) {
            highest_texcoord = std::max(highest_texcoord, set);
            have_texcoord = true;
        } else if (semanticIndex(semantic, "COLOR", &set)) {
            highest_color = std::max(highest_color, set);
            have_color = true;
        } else if (semanticIndex(semantic, "JOINTS", &set)) {
            highest_joint = std::max(highest_joint, set);
            have_joint = true;
        } else if (semanticIndex(semantic, "WEIGHTS", &set)) {
            highest_weight = std::max(highest_weight, set);
            have_weight = true;
        }
        (void)accessor_value;
    }
    if (have_texcoord) mesh.texcoord_sets.resize(highest_texcoord + 1u);
    if (have_color) mesh.color_sets.resize(highest_color + 1u);
    if (have_joint) mesh.joint_sets.resize(highest_joint + 1u);
    if (have_weight) mesh.weight_sets.resize(highest_weight + 1u);

    for (const auto& [semantic, accessor_value] : attributes->object) {
        std::size_t set = 0u;
        const int accessor = GltfJson::integer(&accessor_value);
        if (semanticIndex(semantic, "TEXCOORD", &set)) {
            if (!decodeVec2Set(context, accessor, vertex_count, &mesh.texcoord_sets[set], error)) return false;
        } else if (semanticIndex(semantic, "COLOR", &set)) {
            if (!decodeVec4Set(context, accessor, vertex_count, &mesh.color_sets[set], error)) return false;
        } else if (semanticIndex(semantic, "JOINTS", &set)) {
            if (!decodeJointSet(context, accessor, vertex_count, &mesh.joint_sets[set], error)) return false;
        } else if (semanticIndex(semantic, "WEIGHTS", &set)) {
            if (!decodeWeightSet(context, accessor, vertex_count, &mesh.weight_sets[set], error)) return false;
        }
    }

    if (!mesh.texcoord_sets.empty() && mesh.texcoord_sets[0].size() == vertex_count)
        for (std::size_t i = 0u; i < vertex_count; ++i) mesh.vertices[i].uv = mesh.texcoord_sets[0][i];
    if (!mesh.color_sets.empty() && mesh.color_sets[0].size() == vertex_count)
        for (std::size_t i = 0u; i < vertex_count; ++i) mesh.vertices[i].color = mesh.color_sets[0][i];
    if (!mesh.joint_sets.empty() && !mesh.weight_sets.empty() &&
        mesh.joint_sets[0].size() == vertex_count && mesh.weight_sets[0].size() == vertex_count) {
        for (std::size_t i = 0u; i < vertex_count; ++i) {
            const Joint4 joints = mesh.joint_sets[0][i];
            const Vec4 weights = mesh.weight_sets[0][i];
            mesh.vertices[i].skin.joints = {joints.x, joints.y, joints.z, joints.w};
            mesh.vertices[i].skin.weights = {weights.x, weights.y, weights.z, weights.w};
        }
    }

    std::vector<std::uint32_t> source_indices;
    const int index_accessor = GltfJson::integer(primitive.get("indices"));
    if (index_accessor >= 0) {
        if (!GltfData::decodeUnsigned(context.assets.data, index_accessor, &source_indices, 1u, error)) return false;
    } else {
        if (vertex_count > std::numeric_limits<std::uint32_t>::max()) return fail(error, "glTF primitive has too many vertices");
        source_indices.resize(vertex_count);
        for (std::size_t i = 0u; i < vertex_count; ++i) source_indices[i] = static_cast<std::uint32_t>(i);
    }
    for (const std::uint32_t index : source_indices)
        if (index >= vertex_count) return fail(error, "glTF primitive index exceeds vertex count");

    mesh.primitive_mode = primitiveMode(GltfJson::integer(primitive.get("mode"), 4));
    mesh.source_indices = source_indices;
    triangleIndices(mesh.primitive_mode, source_indices, &mesh.indices);
    if (mesh.primitive_mode == PrimitiveMode::Triangles && (mesh.indices.size() % 3u) != 0u)
        return fail(error, "glTF TRIANGLES index count is not divisible by three");
    if (!has_normals && !mesh.indices.empty()) generatedNormals(&mesh);
    if (!parseMorphTargets(context, primitive.get("targets"), vertex_count, &mesh, error)) return false;
    meshWeightsAndNames(mesh_source, &mesh);
    preserveObject(&mesh_source, &mesh.extras_json, nullptr);
    preserveObject(&primitive, nullptr, &mesh.extensions_json);
    bounds(&mesh);
    *out = std::move(mesh);
    return true;
}

MaterialData materialFor(Context& context, const Value& primitive, std::string *error)
{
    const int index = GltfJson::integer(primitive.get("material"));
    if (index < 0) {
        MaterialData material;
        material.roughness = 1.0f;
        material.metallic = 1.0f;
        return material;
    }
    if (static_cast<std::size_t>(index) >= context.assets.materials.size()) {
        if (error) *error = "glTF primitive references invalid material";
        return {};
    }
    return context.assets.materials[static_cast<std::size_t>(index)];
}

bool variantMappings(
    Context& context,
    const Value& primitive,
    std::uint32_t part,
    Document *output,
    std::string *error)
{
    const Value *extensions = primitive.get("extensions");
    if (!extensions || !extensions->is(Type::Object)) return true;
    const Value *variants = extensions->get("KHR_materials_variants");
    if (!variants) return true;
    if (!variants->is(Type::Object)) return fail(error, "KHR_materials_variants primitive extension must be an object");
    const Value *mappings = variants->get("mappings");
    if (!mappings || !mappings->is(Type::Array)) return fail(error, "KHR_materials_variants mappings must be an array");
    for (const Value& mapping : mappings->array) {
        if (!mapping.is(Type::Object)) return fail(error, "invalid material variant mapping");
        const int material_index = GltfJson::integer(mapping.get("material"));
        if (material_index < 0 || static_cast<std::size_t>(material_index) >= context.assets.materials.size())
            return fail(error, "material variant mapping references invalid material");
        const Value *indices = mapping.get("variants");
        if (!indices || !indices->is(Type::Array)) return fail(error, "material variant mapping has no variants array");
        VariantMaterial result;
        result.part = part;
        result.material = context.assets.materials[static_cast<std::size_t>(material_index)];
        for (const Value& value : indices->array) {
            const std::uint32_t variant = GltfJson::unsignedInteger(&value);
            if (variant == UINT32_MAX || variant >= output->variants.size())
                return fail(error, "material variant mapping references invalid variant");
            result.variants.push_back(variant);
        }
        output->variant_materials.push_back(std::move(result));
    }
    return true;
}

bool appendMeshParts(Context& context, int mesh_index, std::uint32_t node_index, Document *output, std::string *error)
{
    const Value *meshes = context.root.get("meshes");
    if (!meshes || !meshes->is(Type::Array) || mesh_index < 0 || static_cast<std::size_t>(mesh_index) >= meshes->array.size())
        return fail(error, "glTF node references invalid mesh");
    const Value& mesh_source = meshes->array[static_cast<std::size_t>(mesh_index)];
    if (!mesh_source.is(Type::Object)) return fail(error, "invalid glTF mesh object");
    const Value *primitives = mesh_source.get("primitives");
    if (!primitives || !primitives->is(Type::Array)) return fail(error, "glTF mesh has no primitives array");

    for (std::size_t primitive_index = 0u; primitive_index < primitives->array.size(); ++primitive_index) {
        const Value& primitive = primitives->array[primitive_index];
        MeshData mesh;
        if (!primitiveMesh(context, mesh_source, primitive, &mesh, error)) return false;
        const int material_index = GltfJson::integer(primitive.get("material"));
        if (material_index >= 0 && static_cast<std::size_t>(material_index) >= context.assets.materials.size())
            return fail(error, "glTF primitive references invalid material");
        Part part;
        part.mesh = std::move(mesh);
        part.material = materialFor(context, primitive, error);
        part.node = node_index;
        part.primitive = static_cast<std::uint32_t>(primitive_index);
        const std::uint32_t part_index = static_cast<std::uint32_t>(output->parts.size());
        output->parts.push_back(std::move(part));
        if (node_index != INVALID_INDEX && node_index < output->nodes.size())
            output->nodes[node_index].parts.push_back(part_index);
        if (!variantMappings(context, primitive, part_index, output, error)) return false;
    }
    return true;
}

void readVec3(const Value *value, Vec3 *out, Vec3 fallback)
{
    if (!out) return;
    *out = fallback;
    if (!value || !value->is(Type::Array) || value->array.size() < 3u) return;
    out->x = GltfJson::floatValue(&value->array[0], fallback.x);
    out->y = GltfJson::floatValue(&value->array[1], fallback.y);
    out->z = GltfJson::floatValue(&value->array[2], fallback.z);
}

void readQuat(const Value *value, Quat *out)
{
    if (!out) return;
    *out = {};
    if (!value || !value->is(Type::Array) || value->array.size() < 4u) return;
    out->x = GltfJson::floatValue(&value->array[0]);
    out->y = GltfJson::floatValue(&value->array[1]);
    out->z = GltfJson::floatValue(&value->array[2]);
    out->w = GltfJson::floatValue(&value->array[3], 1.0f);
}

bool parseCameras(Context& context, Document *output, std::string *error)
{
    const Value *cameras = context.root.get("cameras");
    if (!cameras) return true;
    if (!cameras->is(Type::Array)) return fail(error, "glTF cameras must be an array");
    output->cameras.reserve(cameras->array.size());
    for (const Value& source : cameras->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF camera object");
        CameraData camera;
        camera.name = GltfJson::stringValue(source.get("name"));
        const std::string type = GltfJson::stringValue(source.get("type"));
        if (type == "orthographic") {
            camera.type = CameraType::Orthographic;
            const Value *data = source.get("orthographic");
            if (!data || !data->is(Type::Object)) return fail(error, "orthographic glTF camera has no data");
            camera.xmag = GltfJson::floatValue(data->get("xmag"), 1.0f);
            camera.ymag = GltfJson::floatValue(data->get("ymag"), 1.0f);
            camera.znear = GltfJson::floatValue(data->get("znear"), 0.1f);
            camera.zfar = GltfJson::floatValue(data->get("zfar"), 1000.0f);
        } else if (type == "perspective") {
            camera.type = CameraType::Perspective;
            const Value *data = source.get("perspective");
            if (!data || !data->is(Type::Object)) return fail(error, "perspective glTF camera has no data");
            camera.yfov = GltfJson::floatValue(data->get("yfov"), 0.78539816339f);
            camera.aspect_ratio = GltfJson::floatValue(data->get("aspectRatio"));
            camera.znear = GltfJson::floatValue(data->get("znear"), 0.1f);
            camera.zfar = GltfJson::floatValue(data->get("zfar"));
        } else {
            return fail(error, "unknown glTF camera type: " + type);
        }
        output->cameras.push_back(std::move(camera));
    }
    return true;
}

bool parseLights(Context& context, Document *output, std::string *error)
{
    const Value *extensions = context.root.get("extensions");
    if (!extensions || !extensions->is(Type::Object)) return true;
    const Value *punctual = extensions->get("KHR_lights_punctual");
    if (!punctual) return true;
    if (!punctual->is(Type::Object)) return fail(error, "KHR_lights_punctual must be an object");
    const Value *lights = punctual->get("lights");
    if (!lights || !lights->is(Type::Array)) return fail(error, "KHR_lights_punctual has no lights array");
    output->lights.reserve(lights->array.size());
    for (const Value& source : lights->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid punctual light object");
        LightData light;
        light.name = GltfJson::stringValue(source.get("name"));
        const std::string type = GltfJson::stringValue(source.get("type"));
        light.type = type == "directional" ? AssetLightType::Directional :
            (type == "spot" ? AssetLightType::Spot : AssetLightType::Point);
        readVec3(source.get("color"), &light.color, {1.0f, 1.0f, 1.0f});
        light.intensity = std::max(GltfJson::floatValue(source.get("intensity"), 1.0f), 0.0f);
        light.range = std::max(GltfJson::floatValue(source.get("range")), 0.0f);
        if (const Value *spot = source.get("spot"); spot && spot->is(Type::Object)) {
            light.inner_cone_angle = std::max(GltfJson::floatValue(spot->get("innerConeAngle")), 0.0f);
            light.outer_cone_angle = std::max(GltfJson::floatValue(spot->get("outerConeAngle"), 0.78539816339f), light.inner_cone_angle);
        }
        const Value *light_extensions = source.get("extensions");
        if (light_extensions && light_extensions->is(Type::Object)) {
            const Value *ies = light_extensions->get("EXT_lights_ies");
            if (ies && ies->is(Type::Object)) light.ies_uri = GltfJson::stringValue(ies->get("uri"));
        }
        output->lights.push_back(std::move(light));
    }
    return true;
}

bool parseVariants(Context& context, Document *output, std::string *error)
{
    const Value *extensions = context.root.get("extensions");
    if (!extensions || !extensions->is(Type::Object)) return true;
    const Value *variants_extension = extensions->get("KHR_materials_variants");
    if (!variants_extension) return true;
    if (!variants_extension->is(Type::Object)) return fail(error, "KHR_materials_variants root extension must be an object");
    const Value *variants = variants_extension->get("variants");
    if (!variants || !variants->is(Type::Array)) return fail(error, "KHR_materials_variants has no variants array");
    output->variants.reserve(variants->array.size());
    for (const Value& source : variants->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid material variant object");
        output->variants.push_back({GltfJson::stringValue(source.get("name"))});
    }
    return true;
}

bool parseNodes(Context& context, Document *output, std::vector<int> *node_meshes, std::string *error)
{
    const Value *nodes = context.root.get("nodes");
    if (!nodes) return true;
    if (!nodes->is(Type::Array)) return fail(error, "glTF nodes must be an array");
    output->nodes.resize(nodes->array.size());
    node_meshes->assign(nodes->array.size(), -1);

    for (std::size_t i = 0u; i < nodes->array.size(); ++i) {
        const Value& source = nodes->array[i];
        if (!source.is(Type::Object)) return fail(error, "invalid glTF node object");
        NodeData& node = output->nodes[i];
        node.name = GltfJson::stringValue(source.get("name"));
        readVec3(source.get("translation"), &node.translation, {});
        readQuat(source.get("rotation"), &node.rotation);
        readVec3(source.get("scale"), &node.scale, {1.0f, 1.0f, 1.0f});
        if (const Value *matrix = source.get("matrix"); matrix) {
            if (!matrix->is(Type::Array) || matrix->array.size() != 16u) return fail(error, "glTF node matrix must contain 16 values");
            for (std::size_t component = 0u; component < 16u; ++component)
                node.matrix[component] = GltfJson::floatValue(&matrix->array[component], component % 5u == 0u ? 1.0f : 0.0f);
            node.has_matrix = true;
        }
        node.skin = GltfJson::unsignedInteger(source.get("skin"));
        node.camera = GltfJson::unsignedInteger(source.get("camera"));
        (*node_meshes)[i] = GltfJson::integer(source.get("mesh"));
        if (const Value *weights = source.get("weights"); weights && weights->is(Type::Array)) {
            node.weights.reserve(weights->array.size());
            for (const Value& weight : weights->array) node.weights.push_back(GltfJson::floatValue(&weight));
        }
        const Value *children = source.get("children");
        if (children) {
            if (!children->is(Type::Array)) return fail(error, "glTF node children must be an array");
            for (const Value& child : children->array) {
                const std::uint32_t index = GltfJson::unsignedInteger(&child);
                if (index == UINT32_MAX || index >= output->nodes.size()) return fail(error, "glTF node references invalid child");
                node.children.push_back(index);
            }
        }

        const Value *extensions = source.get("extensions");
        if (extensions && extensions->is(Type::Object)) {
            if (const Value *light = extensions->get("KHR_lights_punctual"); light && light->is(Type::Object))
                node.light = GltfJson::unsignedInteger(light->get("light"));
            if (const Value *visibility = extensions->get("KHR_node_visibility"); visibility && visibility->is(Type::Object))
                node.visible = GltfJson::boolValue(visibility->get("visible"), true);
            if (const Value *selectability = extensions->get("KHR_node_selectability"); selectability && selectability->is(Type::Object))
                node.selectable = GltfJson::boolValue(selectability->get("selectable"), true);
            if (const Value *hoverability = extensions->get("KHR_node_hoverability"); hoverability && hoverability->is(Type::Object))
                node.hoverable = GltfJson::boolValue(hoverability->get("hoverable"), true);
        }
        preserveObject(&source, &node.extras_json, &node.extensions_json);
    }

    for (std::size_t parent = 0u; parent < output->nodes.size(); ++parent) {
        for (const std::uint32_t child : output->nodes[parent].children) {
            if (output->nodes[child].parent >= 0) return fail(error, "glTF node has multiple parents");
            output->nodes[child].parent = static_cast<std::int32_t>(parent);
        }
    }
    return true;
}

bool parseScenes(Context& context, Document *output, std::string *error)
{
    const Value *scenes = context.root.get("scenes");
    if (scenes) {
        if (!scenes->is(Type::Array)) return fail(error, "glTF scenes must be an array");
        output->scenes.reserve(scenes->array.size());
        for (const Value& source : scenes->array) {
            if (!source.is(Type::Object)) return fail(error, "invalid glTF scene object");
            SceneData scene;
            scene.name = GltfJson::stringValue(source.get("name"));
            const Value *nodes = source.get("nodes");
            if (nodes) {
                if (!nodes->is(Type::Array)) return fail(error, "glTF scene nodes must be an array");
                for (const Value& node : nodes->array) {
                    const std::uint32_t index = GltfJson::unsignedInteger(&node);
                    if (index == UINT32_MAX || index >= output->nodes.size()) return fail(error, "glTF scene references invalid node");
                    scene.nodes.push_back(index);
                }
            }
            preserveObject(&source, &scene.extras_json, &scene.extensions_json);
            output->scenes.push_back(std::move(scene));
        }
        output->default_scene = GltfJson::unsignedInteger(context.root.get("scene"), output->scenes.empty() ? INVALID_INDEX : 0u);
        if (output->default_scene != INVALID_INDEX && output->default_scene >= output->scenes.size())
            return fail(error, "glTF default scene index is invalid");
    }

    if (output->scenes.empty() && !output->nodes.empty()) {
        SceneData scene;
        scene.name = "implicit";
        for (std::size_t i = 0u; i < output->nodes.size(); ++i)
            if (output->nodes[i].parent < 0) scene.nodes.push_back(static_cast<std::uint32_t>(i));
        output->scenes.push_back(std::move(scene));
        output->default_scene = 0u;
    }
    return true;
}

bool parseInstances(Context& context, Document *output, std::string *error)
{
    const Value *nodes = context.root.get("nodes");
    if (!nodes || !nodes->is(Type::Array)) return true;
    for (std::size_t node_index = 0u; node_index < nodes->array.size(); ++node_index) {
        const Value *extensions = nodes->array[node_index].get("extensions");
        if (!extensions || !extensions->is(Type::Object)) continue;
        const Value *instancing = extensions->get("EXT_mesh_gpu_instancing");
        if (!instancing) continue;
        if (!instancing->is(Type::Object)) return fail(error, "EXT_mesh_gpu_instancing must be an object");
        const Value *attributes = instancing->get("attributes");
        if (!attributes || !attributes->is(Type::Object)) return fail(error, "EXT_mesh_gpu_instancing has no attributes object");
        InstanceData instance;
        instance.node = static_cast<std::uint32_t>(node_index);
        std::size_t count = 0u;
        bool count_set = false;
        for (const auto& [semantic, accessor_value] : attributes->object) {
            const int accessor = GltfJson::integer(&accessor_value);
            if (accessor < 0 || static_cast<std::size_t>(accessor) >= context.assets.data.accessors.size())
                return fail(error, "GPU instancing references invalid accessor");
            const std::size_t accessor_count = context.assets.data.accessors[static_cast<std::size_t>(accessor)].count;
            if (!count_set) {
                count = accessor_count;
                count_set = true;
            } else if (count != accessor_count) {
                return fail(error, "GPU instancing attribute counts do not match");
            }
            const std::size_t components = accessorComponents(context, accessor);
            std::vector<float> values;
            if (!GltfData::decodeFloats(context.assets.data, accessor, &values, components, error)) return false;
            if (semantic == "TRANSLATION" && components == 3u) {
                instance.translations.resize(count);
                for (std::size_t i = 0u; i < count; ++i)
                    instance.translations[i] = {values[i * 3u], values[i * 3u + 1u], values[i * 3u + 2u]};
            } else if (semantic == "ROTATION" && components == 4u) {
                instance.rotations.resize(count);
                for (std::size_t i = 0u; i < count; ++i)
                    instance.rotations[i] = {values[i * 4u], values[i * 4u + 1u], values[i * 4u + 2u], values[i * 4u + 3u]};
            } else if (semantic == "SCALE" && components == 3u) {
                instance.scales.resize(count);
                for (std::size_t i = 0u; i < count; ++i)
                    instance.scales[i] = {values[i * 3u], values[i * 3u + 1u], values[i * 3u + 2u]};
            } else {
                instance.attributes.emplace(semantic, std::move(values));
            }
        }
        output->instances.push_back(std::move(instance));
    }
    return true;
}

bool parseSkins(Context& context, Document *output, std::string *error)
{
    const Value *skins = context.root.get("skins");
    if (!skins) return true;
    if (!skins->is(Type::Array)) return fail(error, "glTF skins must be an array");
    output->skins.reserve(skins->array.size());
    for (const Value& source : skins->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF skin object");
        SkinData skin;
        skin.name = GltfJson::stringValue(source.get("name"));
        skin.skeleton = GltfJson::unsignedInteger(source.get("skeleton"));
        const Value *joints = source.get("joints");
        if (!joints || !joints->is(Type::Array) || joints->array.empty()) return fail(error, "glTF skin has no joints");
        skin.joints.reserve(joints->array.size());
        for (const Value& joint : joints->array) {
            const std::uint32_t index = GltfJson::unsignedInteger(&joint);
            if (index == UINT32_MAX || index >= output->nodes.size()) return fail(error, "glTF skin references invalid joint node");
            skin.joints.push_back(index);
        }

        const int accessor = GltfJson::integer(source.get("inverseBindMatrices"));
        if (accessor >= 0) {
            std::vector<float> values;
            if (!GltfData::decodeFloats(context.assets.data, accessor, &values, 16u, error)) return false;
            if (values.size() / 16u != skin.joints.size()) return fail(error, "inverseBindMatrices count does not match skin joint count");
            skin.inverse_bind_matrices.resize(skin.joints.size());
            for (std::size_t matrix = 0u; matrix < skin.joints.size(); ++matrix)
                for (std::size_t component = 0u; component < 16u; ++component)
                    skin.inverse_bind_matrices[matrix][component] = values[matrix * 16u + component];
        } else {
            skin.inverse_bind_matrices.assign(skin.joints.size(), identityMatrix());
        }
        output->skins.push_back(std::move(skin));
    }

    if (!output->skins.empty()) {
        const SkinData& source = output->skins.front();
        output->skeleton.name = source.name;
        output->skeleton.bones.resize(source.joints.size());
        std::unordered_map<std::uint32_t, std::int32_t> joint_to_bone;
        for (std::size_t i = 0u; i < source.joints.size(); ++i)
            joint_to_bone.emplace(source.joints[i], static_cast<std::int32_t>(i));
        for (std::size_t i = 0u; i < source.joints.size(); ++i) {
            const std::uint32_t node_index = source.joints[i];
            const NodeData& node = output->nodes[node_index];
            Animation::Bone& bone = output->skeleton.bones[i];
            bone.name = node.name;
            bone.bind_local.translation = {node.translation.x, node.translation.y, node.translation.z};
            bone.bind_local.rotation = {node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w};
            bone.bind_local.scale = {node.scale.x, node.scale.y, node.scale.z};
            for (std::size_t component = 0u; component < 16u; ++component)
                bone.inverse_bind.value[component] = source.inverse_bind_matrices[i][component];
            std::int32_t parent = node.parent;
            while (parent >= 0) {
                const auto found = joint_to_bone.find(static_cast<std::uint32_t>(parent));
                if (found != joint_to_bone.end()) {
                    bone.parent = found->second;
                    break;
                }
                parent = output->nodes[static_cast<std::size_t>(parent)].parent;
            }
        }
        output->has_skeleton = true;
    }
    return true;
}

AnimationInterpolation interpolation(std::string_view name)
{
    if (name == "STEP") return AnimationInterpolation::Step;
    if (name == "CUBICSPLINE") return AnimationInterpolation::CubicSpline;
    return AnimationInterpolation::Linear;
}

bool parseAnimations(Context& context, Document *output, std::string *error)
{
    const Value *animations = context.root.get("animations");
    if (!animations) return true;
    if (!animations->is(Type::Array)) return fail(error, "glTF animations must be an array");
    output->model_animations.reserve(animations->array.size());

    for (std::size_t animation_index = 0u; animation_index < animations->array.size(); ++animation_index) {
        const Value& source = animations->array[animation_index];
        if (!source.is(Type::Object)) return fail(error, "invalid glTF animation object");
        ModelAnimationData animation;
        animation.name = GltfJson::stringValue(source.get("name"), "animation_" + std::to_string(animation_index));
        const Value *samplers = source.get("samplers");
        const Value *channels = source.get("channels");
        if (!samplers || !samplers->is(Type::Array) || !channels || !channels->is(Type::Array))
            return fail(error, "glTF animation must contain samplers and channels arrays");

        animation.samplers.reserve(samplers->array.size());
        for (const Value& sampler_source : samplers->array) {
            if (!sampler_source.is(Type::Object)) return fail(error, "invalid glTF animation sampler");
            const int input_accessor = GltfJson::integer(sampler_source.get("input"));
            const int output_accessor = GltfJson::integer(sampler_source.get("output"));
            if (input_accessor < 0 || output_accessor < 0) return fail(error, "glTF animation sampler has invalid accessors");
            AnimationSamplerData sampler;
            sampler.interpolation = interpolation(GltfJson::stringValue(sampler_source.get("interpolation"), "LINEAR"));
            if (!GltfData::decodeFloats(context.assets.data, input_accessor, &sampler.input, 1u, error)) return false;
            sampler.components = static_cast<std::uint32_t>(accessorComponents(context, output_accessor));
            if (sampler.components == 0u || !GltfData::decodeFloats(
                context.assets.data,
                output_accessor,
                &sampler.output,
                sampler.components,
                error
            )) return false;
            for (const float time : sampler.input) animation.duration = std::max(animation.duration, time);
            animation.samplers.push_back(std::move(sampler));
        }

        animation.channels.reserve(channels->array.size());
        for (const Value& channel_source : channels->array) {
            if (!channel_source.is(Type::Object)) return fail(error, "invalid glTF animation channel");
            AnimationChannelData channel;
            channel.sampler = GltfJson::unsignedInteger(channel_source.get("sampler"));
            if (channel.sampler == INVALID_INDEX || channel.sampler >= animation.samplers.size())
                return fail(error, "glTF animation channel references invalid sampler");
            const Value *target = channel_source.get("target");
            if (!target || !target->is(Type::Object)) return fail(error, "glTF animation channel has invalid target");
            channel.node = GltfJson::unsignedInteger(target->get("node"));
            const std::string path = GltfJson::stringValue(target->get("path"));
            if (path == "translation") channel.path = AnimationPath::Translation;
            else if (path == "rotation") channel.path = AnimationPath::Rotation;
            else if (path == "scale") channel.path = AnimationPath::Scale;
            else if (path == "weights") channel.path = AnimationPath::Weights;
            else {
                const Value *extensions = target->get("extensions");
                const Value *pointer = extensions && extensions->is(Type::Object)
                    ? extensions->get("KHR_animation_pointer") : nullptr;
                if (!pointer || !pointer->is(Type::Object)) return fail(error, "unsupported glTF animation target path: " + path);
                channel.path = AnimationPath::Pointer;
                channel.pointer = GltfJson::stringValue(pointer->get("pointer"));
                if (channel.pointer.empty()) return fail(error, "KHR_animation_pointer target has empty pointer");
            }
            if (channel.path != AnimationPath::Pointer &&
                (channel.node == INVALID_INDEX || channel.node >= output->nodes.size()))
                return fail(error, "glTF animation channel references invalid node");
            animation.channels.push_back(std::move(channel));
        }
        output->model_animations.push_back(std::move(animation));
    }
    return true;
}

bool buildParts(Context& context, const std::vector<int>& node_meshes, Document *output, std::string *error)
{
    std::vector<std::uint8_t> referenced;
    const Value *meshes = context.root.get("meshes");
    if (meshes && meshes->is(Type::Array)) referenced.assign(meshes->array.size(), 0u);
    for (std::size_t node = 0u; node < node_meshes.size(); ++node) {
        const int mesh = node_meshes[node];
        if (mesh < 0) continue;
        if (static_cast<std::size_t>(mesh) >= referenced.size()) return fail(error, "glTF node references invalid mesh");
        referenced[static_cast<std::size_t>(mesh)] = 1u;
        if (!appendMeshParts(context, mesh, static_cast<std::uint32_t>(node), output, error)) return false;
    }
    if (meshes && meshes->is(Type::Array)) {
        for (std::size_t mesh = 0u; mesh < meshes->array.size(); ++mesh) {
            if (mesh < referenced.size() && referenced[mesh] != 0u) continue;
            if (!appendMeshParts(context, static_cast<int>(mesh), INVALID_INDEX, output, error)) return false;
        }
    }
    return true;
}

void attachSkinInverseBinds(Document *output)
{
    if (!output) return;
    for (const NodeData& node : output->nodes) {
        if (node.skin == INVALID_INDEX || node.skin >= output->skins.size()) continue;
        const SkinData& skin = output->skins[node.skin];
        for (const std::uint32_t part_index : node.parts) {
            if (part_index >= output->parts.size()) continue;
            MeshData& mesh = output->parts[part_index].mesh;
            mesh.skin_inverse_bind.resize(skin.inverse_bind_matrices.size());
            for (std::size_t matrix = 0u; matrix < skin.inverse_bind_matrices.size(); ++matrix)
                for (std::size_t component = 0u; component < 16u; ++component)
                    mesh.skin_inverse_bind[matrix].value[component] = skin.inverse_bind_matrices[matrix][component];
        }
    }
}

} // namespace

bool load(const std::string& path, Document *output, std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error, "null glTF output");
    *output = {};

    std::string json;
    std::vector<std::uint8_t> binary;
    if (!GltfData::parseContainer(path, &json, &binary, error)) return false;

    Context context;
    if (!GltfJson::parse(json, &context.root, error)) return false;
    if (!context.root.is(Type::Object)) return fail(error, "glTF root must be an object");
    const Value *asset = context.root.get("asset");
    if (!asset || !asset->is(Type::Object)) return fail(error, "glTF has no asset object");
    const std::string version = GltfJson::stringValue(asset->get("version"));
    if (version.empty() || version[0] != '2') return fail(error, "Horse supports glTF 2.x assets only");
    if (!validateRequiredExtensions(context.root, error)) return false;

    context.assets.data.path = path;
    context.assets.data.directory = std::filesystem::path(path).parent_path();
    context.assets.data.root = &context.root;
    if (!GltfData::loadBuffers(&context.assets.data, binary, error) ||
        !GltfData::loadViews(&context.assets.data, error) ||
        !GltfData::loadAccessors(&context.assets.data, error) ||
        !GltfAssets::load(&context.assets, error))
        return false;

    preserveObject(&context.root, &output->extras_json, &output->extensions_json);
    if (!parseVariants(context, output, error) ||
        !parseCameras(context, output, error) ||
        !parseLights(context, output, error))
        return false;

    std::vector<int> node_meshes;
    if (!parseNodes(context, output, &node_meshes, error) ||
        !parseScenes(context, output, error) ||
        !buildParts(context, node_meshes, output, error) ||
        !parseSkins(context, output, error) ||
        !parseInstances(context, output, error) ||
        !parseAnimations(context, output, error))
        return false;

    attachSkinInverseBinds(output);
    if (output->parts.empty()) return fail(error, "glTF contains no mesh primitives: " + path);
    return true;
}

} // namespace Models::Formats::GltfFull
