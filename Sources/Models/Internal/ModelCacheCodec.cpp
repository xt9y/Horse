#include "Models/Internal/ModelCacheCodec.hpp"

#include "Models/Internal/ModelCacheBinary.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Models::Internal::ModelCacheCodec {
namespace {

using ModelCacheBinary::Reader;
using ModelCacheBinary::Writer;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

template <typename T, typename Write>
void writeVector(Writer& out, const std::vector<T>& values, Write write)
{
    if (!out.count(values.size())) return;
    for (const T& value : values) write(out, value);
}

template <typename T, typename Read>
bool readVector(Reader& in, std::vector<T> *values, Read read)
{
    if (!values) return false;
    std::size_t count = 0u;
    if (!in.count(&count)) return false;
    values->clear();
    values->resize(count);
    for (T& value : *values)
        if (!read(in, &value)) return false;
    return true;
}

void writeVec2(Writer& out, Vec2 value)
{
    out.f32(value.x); out.f32(value.y);
}

bool readVec2(Reader& in, Vec2 *value)
{
    return value && in.f32(&value->x) && in.f32(&value->y);
}

void writeVec3(Writer& out, Vec3 value)
{
    out.f32(value.x); out.f32(value.y); out.f32(value.z);
}

bool readVec3(Reader& in, Vec3 *value)
{
    return value && in.f32(&value->x) && in.f32(&value->y) && in.f32(&value->z);
}

void writeVec4(Writer& out, Vec4 value)
{
    out.f32(value.x); out.f32(value.y); out.f32(value.z); out.f32(value.w);
}

bool readVec4(Reader& in, Vec4 *value)
{
    return value && in.f32(&value->x) && in.f32(&value->y) &&
        in.f32(&value->z) && in.f32(&value->w);
}

void writeQuat(Writer& out, Quat value)
{
    out.f32(value.x); out.f32(value.y); out.f32(value.z); out.f32(value.w);
}

bool readQuat(Reader& in, Quat *value)
{
    return value && in.f32(&value->x) && in.f32(&value->y) &&
        in.f32(&value->z) && in.f32(&value->w);
}

void writeMat4(Writer& out, const Mat4& value)
{
    for (float component : value) out.f32(component);
}

bool readMat4(Reader& in, Mat4 *value)
{
    if (!value) return false;
    for (float& component : *value)
        if (!in.f32(&component)) return false;
    return true;
}

void writeAnimationVec3(Writer& out, Animation::Vec3 value)
{
    out.f32(value.x); out.f32(value.y); out.f32(value.z);
}

bool readAnimationVec3(Reader& in, Animation::Vec3 *value)
{
    return value && in.f32(&value->x) && in.f32(&value->y) && in.f32(&value->z);
}

void writeAnimationQuat(Writer& out, Animation::Quat value)
{
    out.f32(value.x); out.f32(value.y); out.f32(value.z); out.f32(value.w);
}

bool readAnimationQuat(Reader& in, Animation::Quat *value)
{
    return value && in.f32(&value->x) && in.f32(&value->y) &&
        in.f32(&value->z) && in.f32(&value->w);
}

void writeAnimationMat4(Writer& out, const Animation::Mat4& value)
{
    for (float component : value.value) out.f32(component);
}

bool readAnimationMat4(Reader& in, Animation::Mat4 *value)
{
    if (!value) return false;
    for (float& component : value->value)
        if (!in.f32(&component)) return false;
    return true;
}

void writeAnimationTransform(Writer& out, const Animation::Transform& value)
{
    writeAnimationVec3(out, value.translation);
    writeAnimationQuat(out, value.rotation);
    writeAnimationVec3(out, value.scale);
}

bool readAnimationTransform(Reader& in, Animation::Transform *value)
{
    return value && readAnimationVec3(in, &value->translation) &&
        readAnimationQuat(in, &value->rotation) && readAnimationVec3(in, &value->scale);
}

void writeFloatVector(Writer& out, const std::vector<float>& values)
{
    writeVector(out, values, [](Writer& writer, float value) { writer.f32(value); });
}

bool readFloatVector(Reader& in, std::vector<float> *values)
{
    return readVector(in, values, [](Reader& reader, float *value) { return reader.f32(value); });
}

void writeU32Vector(Writer& out, const std::vector<std::uint32_t>& values)
{
    writeVector(out, values, [](Writer& writer, std::uint32_t value) { writer.u32(value); });
}

bool readU32Vector(Reader& in, std::vector<std::uint32_t> *values)
{
    return readVector(in, values, [](Reader& reader, std::uint32_t *value) { return reader.u32(value); });
}

void writeStringVector(Writer& out, const std::vector<std::string>& values)
{
    writeVector(out, values, [](Writer& writer, const std::string& value) { writer.string(value); });
}

bool readStringVector(Reader& in, std::vector<std::string> *values)
{
    return readVector(in, values, [](Reader& reader, std::string *value) { return reader.string(value); });
}

void writeStringMap(Writer& out, const std::unordered_map<std::string, std::string>& values)
{
    std::vector<std::string> keys;
    keys.reserve(values.size());
    for (const auto& [key, value] : values) {
        (void)value;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    if (!out.count(keys.size())) return;
    for (const std::string& key : keys) {
        out.string(key);
        out.string(values.at(key));
    }
}

bool readStringMap(Reader& in, std::unordered_map<std::string, std::string> *values)
{
    if (!values) return false;
    std::size_t count = 0u;
    if (!in.count(&count)) return false;
    values->clear();
    for (std::size_t index = 0u; index < count; ++index) {
        std::string key;
        std::string value;
        if (!in.string(&key) || !in.string(&value)) return false;
        values->insert_or_assign(std::move(key), std::move(value));
    }
    return true;
}

void writeAttribute(Writer& out, const AttributeData& value)
{
    out.i32(value.component_type);
    out.u32(value.components);
    out.boolean(value.normalized);
    writeVector(out, value.values, [](Writer& writer, double item) { writer.f64(item); });
}

bool readAttribute(Reader& in, AttributeData *value)
{
    if (!value) return false;
    return in.i32(&value->component_type) && in.u32(&value->components) &&
        in.boolean(&value->normalized) &&
        readVector(in, &value->values, [](Reader& reader, double *item) { return reader.f64(item); });
}

void writeAttributeMap(Writer& out, const std::unordered_map<std::string, AttributeData>& values)
{
    std::vector<std::string> keys;
    keys.reserve(values.size());
    for (const auto& [key, value] : values) {
        (void)value;
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    if (!out.count(keys.size())) return;
    for (const std::string& key : keys) {
        out.string(key);
        writeAttribute(out, values.at(key));
    }
}

bool readAttributeMap(Reader& in, std::unordered_map<std::string, AttributeData> *values)
{
    if (!values) return false;
    std::size_t count = 0u;
    if (!in.count(&count)) return false;
    values->clear();
    for (std::size_t index = 0u; index < count; ++index) {
        std::string key;
        AttributeData value;
        if (!in.string(&key) || !readAttribute(in, &value)) return false;
        values->insert_or_assign(std::move(key), std::move(value));
    }
    return true;
}

void writeSkinWeights(Writer& out, const Animation::SkinWeights& value)
{
    for (std::uint16_t joint : value.joints) out.u16(joint);
    for (float weight : value.weights) out.f32(weight);
}

bool readSkinWeights(Reader& in, Animation::SkinWeights *value)
{
    if (!value) return false;
    for (std::uint16_t& joint : value->joints)
        if (!in.u16(&joint)) return false;
    for (float& weight : value->weights)
        if (!in.f32(&weight)) return false;
    return true;
}

void writeVertex(Writer& out, const Vertex& value)
{
    writeVec3(out, value.position);
    writeVec3(out, value.normal);
    writeVec2(out, value.uv);
    writeSkinWeights(out, value.skin);
    writeVec4(out, value.tangent);
    writeVec4(out, value.color);
}

bool readVertex(Reader& in, Vertex *value)
{
    return value && readVec3(in, &value->position) && readVec3(in, &value->normal) &&
        readVec2(in, &value->uv) && readSkinWeights(in, &value->skin) &&
        readVec4(in, &value->tangent) && readVec4(in, &value->color);
}

void writeJoint4(Writer& out, const Joint4& value)
{
    out.u16(value.x); out.u16(value.y); out.u16(value.z); out.u16(value.w);
}

bool readJoint4(Reader& in, Joint4 *value)
{
    return value && in.u16(&value->x) && in.u16(&value->y) &&
        in.u16(&value->z) && in.u16(&value->w);
}

void writeMorphTarget(Writer& out, const MorphTargetData& value)
{
    writeVector(out, value.positions, [](Writer& writer, Vec3 item) { writeVec3(writer, item); });
    writeVector(out, value.normals, [](Writer& writer, Vec3 item) { writeVec3(writer, item); });
    writeVector(out, value.tangents, [](Writer& writer, Vec3 item) { writeVec3(writer, item); });
    writeAttributeMap(out, value.attributes);
}

bool readMorphTarget(Reader& in, MorphTargetData *value)
{
    return value &&
        readVector(in, &value->positions, [](Reader& reader, Vec3 *item) { return readVec3(reader, item); }) &&
        readVector(in, &value->normals, [](Reader& reader, Vec3 *item) { return readVec3(reader, item); }) &&
        readVector(in, &value->tangents, [](Reader& reader, Vec3 *item) { return readVec3(reader, item); }) &&
        readAttributeMap(in, &value->attributes);
}

void writeMesh(Writer& out, const MeshData& value)
{
    writeVector(out, value.vertices, [](Writer& writer, const Vertex& item) { writeVertex(writer, item); });
    writeU32Vector(out, value.indices);
    writeVec3(out, value.bounds.minimum);
    writeVec3(out, value.bounds.maximum);
    writeVector(out, value.skin_inverse_bind,
        [](Writer& writer, const Animation::Mat4& item) { writeAnimationMat4(writer, item); });
    out.u8(static_cast<std::uint8_t>(value.primitive_mode));
    writeU32Vector(out, value.source_indices);
    writeVector(out, value.texcoord_sets, [](Writer& writer, const std::vector<Vec2>& set) {
        writeVector(writer, set, [](Writer& nested, Vec2 item) { writeVec2(nested, item); });
    });
    writeVector(out, value.color_sets, [](Writer& writer, const std::vector<Vec4>& set) {
        writeVector(writer, set, [](Writer& nested, Vec4 item) { writeVec4(nested, item); });
    });
    writeVector(out, value.joint_sets, [](Writer& writer, const std::vector<Joint4>& set) {
        writeVector(writer, set, [](Writer& nested, const Joint4& item) { writeJoint4(nested, item); });
    });
    writeVector(out, value.weight_sets, [](Writer& writer, const std::vector<Vec4>& set) {
        writeVector(writer, set, [](Writer& nested, Vec4 item) { writeVec4(nested, item); });
    });
    writeAttributeMap(out, value.attributes);
    writeVector(out, value.morph_targets,
        [](Writer& writer, const MorphTargetData& item) { writeMorphTarget(writer, item); });
    writeFloatVector(out, value.morph_weights);
    writeStringVector(out, value.morph_names);
    out.string(value.extras_json);
    writeStringMap(out, value.extensions_json);
}

bool readMesh(Reader& in, MeshData *value)
{
    if (!value) return false;
    std::uint8_t mode = 0u;
    if (!readVector(in, &value->vertices, [](Reader& reader, Vertex *item) { return readVertex(reader, item); }) ||
        !readU32Vector(in, &value->indices) ||
        !readVec3(in, &value->bounds.minimum) || !readVec3(in, &value->bounds.maximum) ||
        !readVector(in, &value->skin_inverse_bind,
            [](Reader& reader, Animation::Mat4 *item) { return readAnimationMat4(reader, item); }) ||
        !in.u8(&mode) || mode > static_cast<std::uint8_t>(PrimitiveMode::TriangleFan))
        return false;
    value->primitive_mode = static_cast<PrimitiveMode>(mode);
    return readU32Vector(in, &value->source_indices) &&
        readVector(in, &value->texcoord_sets, [](Reader& reader, std::vector<Vec2> *set) {
            return readVector(reader, set, [](Reader& nested, Vec2 *item) { return readVec2(nested, item); });
        }) &&
        readVector(in, &value->color_sets, [](Reader& reader, std::vector<Vec4> *set) {
            return readVector(reader, set, [](Reader& nested, Vec4 *item) { return readVec4(nested, item); });
        }) &&
        readVector(in, &value->joint_sets, [](Reader& reader, std::vector<Joint4> *set) {
            return readVector(reader, set, [](Reader& nested, Joint4 *item) { return readJoint4(nested, item); });
        }) &&
        readVector(in, &value->weight_sets, [](Reader& reader, std::vector<Vec4> *set) {
            return readVector(reader, set, [](Reader& nested, Vec4 *item) { return readVec4(nested, item); });
        }) &&
        readAttributeMap(in, &value->attributes) &&
        readVector(in, &value->morph_targets,
            [](Reader& reader, MorphTargetData *item) { return readMorphTarget(reader, item); }) &&
        readFloatVector(in, &value->morph_weights) && readStringVector(in, &value->morph_names) &&
        in.string(&value->extras_json) && readStringMap(in, &value->extensions_json);
}

void writeSampler(Writer& out, const SamplerData& value)
{
    out.i32(value.mag_filter); out.i32(value.min_filter);
    out.i32(value.wrap_s); out.i32(value.wrap_t);
}

bool readSampler(Reader& in, SamplerData *value)
{
    return value && in.i32(&value->mag_filter) && in.i32(&value->min_filter) &&
        in.i32(&value->wrap_s) && in.i32(&value->wrap_t);
}

void writeTextureTransform(Writer& out, const TextureTransform& value)
{
    writeVec2(out, value.offset);
    writeVec2(out, value.scale);
    out.f32(value.rotation);
    out.i32(value.texcoord);
}

bool readTextureTransform(Reader& in, TextureTransform *value)
{
    return value && readVec2(in, &value->offset) && readVec2(in, &value->scale) &&
        in.f32(&value->rotation) && in.i32(&value->texcoord);
}

struct TextureTable {
    std::vector<TextureSourceDescriptor> descriptors;
    std::unordered_map<TextureHandle, std::uint32_t> ids;
    std::unordered_set<TextureHandle> visiting;
    std::vector<std::string> file_dependencies;
    std::string error;
    bool ok = true;

    std::uint32_t collect(TextureHandle handle)
    {
        if (handle == INVALID_TEXTURE) return INVALID_INDEX;
        if (const auto found = ids.find(handle); found != ids.end()) return found->second;
        if (!visiting.insert(handle).second) {
            ok = false;
            error = "cyclic deferred texture dependency";
            return INVALID_INDEX;
        }

        TextureSourceDescriptor descriptor;
        if (!textureDescriptor(handle, &descriptor)) {
            visiting.erase(handle);
            ok = false;
            error = "model cache texture has no deferred source descriptor";
            return INVALID_INDEX;
        }

        if (descriptor.source != INVALID_TEXTURE) {
            const std::uint32_t source = collect(descriptor.source);
            if (!ok) return INVALID_INDEX;
            descriptor.source = source;
        }
        if (descriptor.secondary != INVALID_TEXTURE) {
            const std::uint32_t secondary = collect(descriptor.secondary);
            if (!ok) return INVALID_INDEX;
            descriptor.secondary = secondary;
        }

        visiting.erase(handle);
        if (descriptor.kind == TextureSourceKind::File &&
            std::find(file_dependencies.begin(), file_dependencies.end(), descriptor.key) == file_dependencies.end())
            file_dependencies.push_back(descriptor.key);

        if (descriptors.size() >= static_cast<std::size_t>(INVALID_INDEX)) {
            ok = false;
            error = "model cache texture table exhausted";
            return INVALID_INDEX;
        }
        const std::uint32_t id = static_cast<std::uint32_t>(descriptors.size());
        descriptors.push_back(std::move(descriptor));
        ids.emplace(handle, id);
        return id;
    }
};

void collectInfo(TextureTable& table, const TextureInfo& info)
{
    table.collect(info.texture);
}

void collectMaterial(TextureTable& table, const MaterialData& value)
{
    const std::array<TextureHandle, 7> legacy {{
        value.diffuse_texture, value.normal_texture, value.roughness_texture,
        value.metallic_texture, value.ambient_occlusion_texture,
        value.emissive_texture, value.opacity_texture,
    }};
    for (TextureHandle handle : legacy) table.collect(handle);

    const std::array<const TextureInfo *, 19> infos {{
        &value.base_color_info, &value.metallic_roughness_info, &value.normal_info,
        &value.occlusion_info, &value.emissive_info, &value.clearcoat_info,
        &value.clearcoat_roughness_info, &value.clearcoat_normal_info,
        &value.sheen_color_info, &value.sheen_roughness_info, &value.transmission_info,
        &value.thickness_info, &value.specular_info, &value.specular_color_info,
        &value.iridescence_info, &value.iridescence_thickness_info,
        &value.anisotropy_info, &value.diffuse_transmission_info,
        &value.diffuse_transmission_color_info,
    }};
    for (const TextureInfo *info : infos) collectInfo(table, *info);
}

std::uint32_t textureId(Writer& out, const TextureTable& table, TextureHandle handle)
{
    if (handle == INVALID_TEXTURE) return INVALID_INDEX;
    const auto found = table.ids.find(handle);
    if (found == table.ids.end()) {
        out.invalidate();
        return INVALID_INDEX;
    }
    return found->second;
}

void writeTextureInfo(Writer& out, const TextureTable& table, const TextureInfo& value)
{
    out.u32(textureId(out, table, value.texture));
    out.u32(value.texcoord);
    writeTextureTransform(out, value.transform);
    writeSampler(out, value.sampler);
    out.f32(value.scale);
}

bool readTextureInfo(Reader& in, TextureInfo *value)
{
    if (!value) return false;
    return in.u32(&value->texture) && in.u32(&value->texcoord) &&
        readTextureTransform(in, &value->transform) && readSampler(in, &value->sampler) &&
        in.f32(&value->scale);
}

void writeMaterial(Writer& out, const TextureTable& table, const MaterialData& value)
{
    out.string(value.name);
    writeVec3(out, value.color);
    out.f32(value.opacity); out.f32(value.roughness); out.f32(value.metallic);
    out.f32(value.ambient_occlusion);
    writeVec3(out, value.emissive_color);
    out.f32(value.emissive_strength); out.f32(value.ior); out.f32(value.clearcoat);
    out.f32(value.clearcoat_roughness); out.u8(static_cast<std::uint8_t>(value.alpha_mode));
    out.f32(value.alpha_cutoff); out.boolean(value.double_sided); out.boolean(value.unlit);

    out.f32(value.normal_scale); out.f32(value.specular); writeVec3(out, value.specular_color);
    out.f32(value.transmission); out.f32(value.thickness); out.f32(value.attenuation_distance);
    writeVec3(out, value.attenuation_color); writeVec3(out, value.sheen_color);
    out.f32(value.sheen_roughness); out.f32(value.anisotropy_strength);
    out.f32(value.anisotropy_rotation); out.f32(value.iridescence); out.f32(value.iridescence_ior);
    out.f32(value.iridescence_thickness_min); out.f32(value.iridescence_thickness_max);
    out.f32(value.dispersion); out.f32(value.diffuse_transmission);
    writeVec3(out, value.diffuse_transmission_color);

    out.string(value.texture_path); out.string(value.normal_texture_path);
    out.string(value.roughness_texture_path); out.string(value.metallic_texture_path);
    out.string(value.ambient_occlusion_texture_path); out.string(value.emissive_texture_path);
    out.string(value.opacity_texture_path);

    out.u32(textureId(out, table, value.diffuse_texture));
    out.u32(textureId(out, table, value.normal_texture));
    out.u32(textureId(out, table, value.roughness_texture));
    out.u32(textureId(out, table, value.metallic_texture));
    out.u32(textureId(out, table, value.ambient_occlusion_texture));
    out.u32(textureId(out, table, value.emissive_texture));
    out.u32(textureId(out, table, value.opacity_texture));

    writeTextureInfo(out, table, value.base_color_info);
    writeTextureInfo(out, table, value.metallic_roughness_info);
    writeTextureInfo(out, table, value.normal_info);
    writeTextureInfo(out, table, value.occlusion_info);
    writeTextureInfo(out, table, value.emissive_info);
    writeTextureInfo(out, table, value.clearcoat_info);
    writeTextureInfo(out, table, value.clearcoat_roughness_info);
    writeTextureInfo(out, table, value.clearcoat_normal_info);
    writeTextureInfo(out, table, value.sheen_color_info);
    writeTextureInfo(out, table, value.sheen_roughness_info);
    writeTextureInfo(out, table, value.transmission_info);
    writeTextureInfo(out, table, value.thickness_info);
    writeTextureInfo(out, table, value.specular_info);
    writeTextureInfo(out, table, value.specular_color_info);
    writeTextureInfo(out, table, value.iridescence_info);
    writeTextureInfo(out, table, value.iridescence_thickness_info);
    writeTextureInfo(out, table, value.anisotropy_info);
    writeTextureInfo(out, table, value.diffuse_transmission_info);
    writeTextureInfo(out, table, value.diffuse_transmission_color_info);

    out.string(value.extras_json);
    writeStringMap(out, value.extensions_json);
}

bool readMaterial(Reader& in, MaterialData *value)
{
    if (!value) return false;
    std::uint8_t alpha = 0u;
    if (!in.string(&value->name) || !readVec3(in, &value->color) ||
        !in.f32(&value->opacity) || !in.f32(&value->roughness) || !in.f32(&value->metallic) ||
        !in.f32(&value->ambient_occlusion) || !readVec3(in, &value->emissive_color) ||
        !in.f32(&value->emissive_strength) || !in.f32(&value->ior) ||
        !in.f32(&value->clearcoat) || !in.f32(&value->clearcoat_roughness) ||
        !in.u8(&alpha) || alpha > static_cast<std::uint8_t>(AlphaMode::Blend) ||
        !in.f32(&value->alpha_cutoff) || !in.boolean(&value->double_sided) ||
        !in.boolean(&value->unlit))
        return false;
    value->alpha_mode = static_cast<AlphaMode>(alpha);

    if (!in.f32(&value->normal_scale) || !in.f32(&value->specular) ||
        !readVec3(in, &value->specular_color) || !in.f32(&value->transmission) ||
        !in.f32(&value->thickness) || !in.f32(&value->attenuation_distance) ||
        !readVec3(in, &value->attenuation_color) || !readVec3(in, &value->sheen_color) ||
        !in.f32(&value->sheen_roughness) || !in.f32(&value->anisotropy_strength) ||
        !in.f32(&value->anisotropy_rotation) || !in.f32(&value->iridescence) ||
        !in.f32(&value->iridescence_ior) || !in.f32(&value->iridescence_thickness_min) ||
        !in.f32(&value->iridescence_thickness_max) || !in.f32(&value->dispersion) ||
        !in.f32(&value->diffuse_transmission) || !readVec3(in, &value->diffuse_transmission_color) ||
        !in.string(&value->texture_path) || !in.string(&value->normal_texture_path) ||
        !in.string(&value->roughness_texture_path) || !in.string(&value->metallic_texture_path) ||
        !in.string(&value->ambient_occlusion_texture_path) || !in.string(&value->emissive_texture_path) ||
        !in.string(&value->opacity_texture_path) ||
        !in.u32(&value->diffuse_texture) || !in.u32(&value->normal_texture) ||
        !in.u32(&value->roughness_texture) || !in.u32(&value->metallic_texture) ||
        !in.u32(&value->ambient_occlusion_texture) || !in.u32(&value->emissive_texture) ||
        !in.u32(&value->opacity_texture))
        return false;

    return readTextureInfo(in, &value->base_color_info) &&
        readTextureInfo(in, &value->metallic_roughness_info) &&
        readTextureInfo(in, &value->normal_info) && readTextureInfo(in, &value->occlusion_info) &&
        readTextureInfo(in, &value->emissive_info) && readTextureInfo(in, &value->clearcoat_info) &&
        readTextureInfo(in, &value->clearcoat_roughness_info) &&
        readTextureInfo(in, &value->clearcoat_normal_info) &&
        readTextureInfo(in, &value->sheen_color_info) &&
        readTextureInfo(in, &value->sheen_roughness_info) &&
        readTextureInfo(in, &value->transmission_info) && readTextureInfo(in, &value->thickness_info) &&
        readTextureInfo(in, &value->specular_info) && readTextureInfo(in, &value->specular_color_info) &&
        readTextureInfo(in, &value->iridescence_info) &&
        readTextureInfo(in, &value->iridescence_thickness_info) &&
        readTextureInfo(in, &value->anisotropy_info) &&
        readTextureInfo(in, &value->diffuse_transmission_info) &&
        readTextureInfo(in, &value->diffuse_transmission_color_info) &&
        in.string(&value->extras_json) && readStringMap(in, &value->extensions_json);
}

void writeBone(Writer& out, const Animation::Bone& value)
{
    out.string(value.name); out.i32(value.parent);
    writeAnimationTransform(out, value.bind_local);
    writeAnimationMat4(out, value.inverse_bind);
}

bool readBone(Reader& in, Animation::Bone *value)
{
    return value && in.string(&value->name) && in.i32(&value->parent) &&
        readAnimationTransform(in, &value->bind_local) && readAnimationMat4(in, &value->inverse_bind);
}

void writeSkeleton(Writer& out, const Animation::Skeleton& value)
{
    out.string(value.name);
    writeVector(out, value.bones, [](Writer& writer, const Animation::Bone& item) { writeBone(writer, item); });
}

bool readSkeleton(Reader& in, Animation::Skeleton *value)
{
    return value && in.string(&value->name) &&
        readVector(in, &value->bones, [](Reader& reader, Animation::Bone *item) { return readBone(reader, item); });
}

void writeTrack(Writer& out, const Animation::Track& value)
{
    writeVector(out, value.samples,
        [](Writer& writer, const Animation::Transform& item) { writeAnimationTransform(writer, item); });
}

bool readTrack(Reader& in, Animation::Track *value)
{
    return value && readVector(in, &value->samples,
        [](Reader& reader, Animation::Transform *item) { return readAnimationTransform(reader, item); });
}

void writeClip(Writer& out, const Animation::AnimationClip& value)
{
    out.string(value.name); out.f32(value.duration); out.f32(value.sample_rate);
    writeVector(out, value.tracks, [](Writer& writer, const Animation::Track& item) { writeTrack(writer, item); });
}

bool readClip(Reader& in, Animation::AnimationClip *value)
{
    return value && in.string(&value->name) && in.f32(&value->duration) &&
        in.f32(&value->sample_rate) &&
        readVector(in, &value->tracks, [](Reader& reader, Animation::Track *item) { return readTrack(reader, item); });
}

void writeNode(Writer& out, const NodeData& value)
{
    out.string(value.name); out.i32(value.parent); writeU32Vector(out, value.children);
    writeU32Vector(out, value.parts); out.u32(value.mesh); out.u32(value.skin);
    out.u32(value.camera); out.u32(value.light); writeVec3(out, value.translation);
    writeQuat(out, value.rotation); writeVec3(out, value.scale); writeMat4(out, value.matrix);
    out.boolean(value.has_matrix); out.boolean(value.visible); out.boolean(value.selectable);
    out.boolean(value.hoverable); writeFloatVector(out, value.weights);
    out.string(value.extras_json); writeStringMap(out, value.extensions_json);
}

bool readNode(Reader& in, NodeData *value)
{
    return value && in.string(&value->name) && in.i32(&value->parent) &&
        readU32Vector(in, &value->children) && readU32Vector(in, &value->parts) &&
        in.u32(&value->mesh) && in.u32(&value->skin) && in.u32(&value->camera) && in.u32(&value->light) &&
        readVec3(in, &value->translation) && readQuat(in, &value->rotation) && readVec3(in, &value->scale) &&
        readMat4(in, &value->matrix) && in.boolean(&value->has_matrix) && in.boolean(&value->visible) &&
        in.boolean(&value->selectable) && in.boolean(&value->hoverable) && readFloatVector(in, &value->weights) &&
        in.string(&value->extras_json) && readStringMap(in, &value->extensions_json);
}

void writeScene(Writer& out, const SceneData& value)
{
    out.string(value.name); writeU32Vector(out, value.nodes);
    out.string(value.extras_json); writeStringMap(out, value.extensions_json);
}

bool readScene(Reader& in, SceneData *value)
{
    return value && in.string(&value->name) && readU32Vector(in, &value->nodes) &&
        in.string(&value->extras_json) && readStringMap(in, &value->extensions_json);
}

void writeSkin(Writer& out, const SkinData& value)
{
    out.string(value.name); out.u32(value.skeleton); writeU32Vector(out, value.joints);
    writeVector(out, value.inverse_bind_matrices,
        [](Writer& writer, const Mat4& item) { writeMat4(writer, item); });
}

bool readSkin(Reader& in, SkinData *value)
{
    return value && in.string(&value->name) && in.u32(&value->skeleton) &&
        readU32Vector(in, &value->joints) &&
        readVector(in, &value->inverse_bind_matrices,
            [](Reader& reader, Mat4 *item) { return readMat4(reader, item); });
}

void writeCamera(Writer& out, const CameraData& value)
{
    out.string(value.name); out.u8(static_cast<std::uint8_t>(value.type));
    out.f32(value.yfov); out.f32(value.aspect_ratio); out.f32(value.znear);
    out.f32(value.zfar); out.f32(value.xmag); out.f32(value.ymag);
}

bool readCamera(Reader& in, CameraData *value)
{
    if (!value) return false;
    std::uint8_t type = 0u;
    if (!in.string(&value->name) || !in.u8(&type) || type > static_cast<std::uint8_t>(CameraType::Orthographic))
        return false;
    value->type = static_cast<CameraType>(type);
    return in.f32(&value->yfov) && in.f32(&value->aspect_ratio) && in.f32(&value->znear) &&
        in.f32(&value->zfar) && in.f32(&value->xmag) && in.f32(&value->ymag);
}

void writeLight(Writer& out, const LightData& value)
{
    out.string(value.name); out.u8(static_cast<std::uint8_t>(value.type)); writeVec3(out, value.color);
    out.f32(value.intensity); out.f32(value.range); out.f32(value.inner_cone_angle);
    out.f32(value.outer_cone_angle); out.string(value.ies_uri);
}

bool readLight(Reader& in, LightData *value)
{
    if (!value) return false;
    std::uint8_t type = 0u;
    if (!in.string(&value->name) || !in.u8(&type) || type > static_cast<std::uint8_t>(AssetLightType::Spot))
        return false;
    value->type = static_cast<AssetLightType>(type);
    return readVec3(in, &value->color) && in.f32(&value->intensity) && in.f32(&value->range) &&
        in.f32(&value->inner_cone_angle) && in.f32(&value->outer_cone_angle) && in.string(&value->ies_uri);
}

void writeAnimationSampler(Writer& out, const AnimationSamplerData& value)
{
    out.u8(static_cast<std::uint8_t>(value.interpolation));
    writeFloatVector(out, value.input); writeFloatVector(out, value.output); out.u32(value.components);
}

bool readAnimationSampler(Reader& in, AnimationSamplerData *value)
{
    if (!value) return false;
    std::uint8_t interpolation = 0u;
    if (!in.u8(&interpolation) || interpolation > static_cast<std::uint8_t>(AnimationInterpolation::CubicSpline))
        return false;
    value->interpolation = static_cast<AnimationInterpolation>(interpolation);
    return readFloatVector(in, &value->input) && readFloatVector(in, &value->output) && in.u32(&value->components);
}

void writeAnimationChannel(Writer& out, const AnimationChannelData& value)
{
    out.u32(value.sampler); out.u32(value.node); out.u8(static_cast<std::uint8_t>(value.path));
    out.string(value.pointer);
}

bool readAnimationChannel(Reader& in, AnimationChannelData *value)
{
    if (!value) return false;
    std::uint8_t path = 0u;
    if (!in.u32(&value->sampler) || !in.u32(&value->node) || !in.u8(&path) ||
        path > static_cast<std::uint8_t>(AnimationPath::Pointer)) return false;
    value->path = static_cast<AnimationPath>(path);
    return in.string(&value->pointer);
}

void writeModelAnimation(Writer& out, const ModelAnimationData& value)
{
    out.string(value.name);
    writeVector(out, value.samplers,
        [](Writer& writer, const AnimationSamplerData& item) { writeAnimationSampler(writer, item); });
    writeVector(out, value.channels,
        [](Writer& writer, const AnimationChannelData& item) { writeAnimationChannel(writer, item); });
    out.f32(value.duration);
}

bool readModelAnimation(Reader& in, ModelAnimationData *value)
{
    return value && in.string(&value->name) &&
        readVector(in, &value->samplers,
            [](Reader& reader, AnimationSamplerData *item) { return readAnimationSampler(reader, item); }) &&
        readVector(in, &value->channels,
            [](Reader& reader, AnimationChannelData *item) { return readAnimationChannel(reader, item); }) &&
        in.f32(&value->duration);
}

void writeInstance(Writer& out, const InstanceData& value)
{
    out.u32(value.node);
    writeVector(out, value.translations, [](Writer& writer, Vec3 item) { writeVec3(writer, item); });
    writeVector(out, value.rotations, [](Writer& writer, Quat item) { writeQuat(writer, item); });
    writeVector(out, value.scales, [](Writer& writer, Vec3 item) { writeVec3(writer, item); });
    writeAttributeMap(out, value.attributes);
}

bool readInstance(Reader& in, InstanceData *value)
{
    return value && in.u32(&value->node) &&
        readVector(in, &value->translations, [](Reader& reader, Vec3 *item) { return readVec3(reader, item); }) &&
        readVector(in, &value->rotations, [](Reader& reader, Quat *item) { return readQuat(reader, item); }) &&
        readVector(in, &value->scales, [](Reader& reader, Vec3 *item) { return readVec3(reader, item); }) &&
        readAttributeMap(in, &value->attributes);
}

void writeDescriptor(Writer& out, const TextureSourceDescriptor& value)
{
    out.u8(static_cast<std::uint8_t>(value.kind));
    out.string(value.key); out.blob(value.bytes); out.u32(value.source); out.u32(value.secondary);
    out.i32(value.channel); out.u8(static_cast<std::uint8_t>(value.alpha_mode));
    out.f32(value.factor); out.f32(value.cutoff);
}

bool readDescriptor(Reader& in, TextureSourceDescriptor *value)
{
    if (!value) return false;
    std::uint8_t kind = 0u;
    std::uint8_t alpha = 0u;
    if (!in.u8(&kind) || kind > static_cast<std::uint8_t>(TextureSourceKind::Opacity) ||
        !in.string(&value->key) || !in.blob(&value->bytes) || !in.u32(&value->source) ||
        !in.u32(&value->secondary) || !in.i32(&value->channel) || !in.u8(&alpha) ||
        alpha > static_cast<std::uint8_t>(AlphaMode::Blend) || !in.f32(&value->factor) ||
        !in.f32(&value->cutoff))
        return false;
    value->kind = static_cast<TextureSourceKind>(kind);
    value->alpha_mode = static_cast<AlphaMode>(alpha);
    return true;
}

void writeDependency(Writer& out, const DependencyStamp& value)
{
    out.string(value.path); out.u64(value.size); out.i64(value.modified); out.boolean(value.exists);
}

bool readDependency(Reader& in, DependencyStamp *value)
{
    return value && in.string(&value->path) && in.u64(&value->size) &&
        in.i64(&value->modified) && in.boolean(&value->exists);
}

void writePart(Writer& out, const TextureTable& table, const Formats::Part& value)
{
    writeMesh(out, value.mesh); writeMaterial(out, table, value.material);
    out.u32(value.node); out.u32(value.primitive); out.u32(value.source_material);
}

bool readPart(Reader& in, Formats::Part *value)
{
    return value && readMesh(in, &value->mesh) && readMaterial(in, &value->material) &&
        in.u32(&value->node) && in.u32(&value->primitive) && in.u32(&value->source_material);
}

void writeVariantMaterial(Writer& out, const TextureTable& table, const Formats::VariantMaterial& value)
{
    out.u32(value.part); out.u32(value.source_material); writeMaterial(out, table, value.material);
    writeU32Vector(out, value.variants);
}

bool readVariantMaterial(Reader& in, Formats::VariantMaterial *value)
{
    return value && in.u32(&value->part) && in.u32(&value->source_material) &&
        readMaterial(in, &value->material) && readU32Vector(in, &value->variants);
}

TextureHandle remapHandle(TextureHandle local, const std::vector<TextureHandle>& handles, bool *ok)
{
    if (local == INVALID_TEXTURE) return INVALID_TEXTURE;
    if (local >= handles.size()) {
        if (ok) *ok = false;
        return INVALID_TEXTURE;
    }
    return handles[local];
}

void remapInfo(TextureInfo *info, const std::vector<TextureHandle>& handles, bool *ok)
{
    if (info) info->texture = remapHandle(info->texture, handles, ok);
}

void remapMaterial(MaterialData *value, const std::vector<TextureHandle>& handles, bool *ok)
{
    if (!value || !ok || !*ok) return;
    value->diffuse_texture = remapHandle(value->diffuse_texture, handles, ok);
    value->normal_texture = remapHandle(value->normal_texture, handles, ok);
    value->roughness_texture = remapHandle(value->roughness_texture, handles, ok);
    value->metallic_texture = remapHandle(value->metallic_texture, handles, ok);
    value->ambient_occlusion_texture = remapHandle(value->ambient_occlusion_texture, handles, ok);
    value->emissive_texture = remapHandle(value->emissive_texture, handles, ok);
    value->opacity_texture = remapHandle(value->opacity_texture, handles, ok);

    const std::array<TextureInfo *, 19> infos {{
        &value->base_color_info, &value->metallic_roughness_info, &value->normal_info,
        &value->occlusion_info, &value->emissive_info, &value->clearcoat_info,
        &value->clearcoat_roughness_info, &value->clearcoat_normal_info,
        &value->sheen_color_info, &value->sheen_roughness_info, &value->transmission_info,
        &value->thickness_info, &value->specular_info, &value->specular_color_info,
        &value->iridescence_info, &value->iridescence_thickness_info,
        &value->anisotropy_info, &value->diffuse_transmission_info,
        &value->diffuse_transmission_color_info,
    }};
    for (TextureInfo *info : infos) remapInfo(info, handles, ok);
}

bool registerTextures(
    const std::vector<TextureSourceDescriptor>& encoded,
    std::vector<TextureHandle> *handles,
    std::string *error)
{
    if (!handles) return false;
    handles->clear();
    handles->reserve(encoded.size());
    for (const TextureSourceDescriptor& source : encoded) {
        TextureSourceDescriptor descriptor = source;
        if (descriptor.source != INVALID_TEXTURE) {
            if (descriptor.source >= handles->size()) return fail(error, "cache texture source is not topologically ordered");
            descriptor.source = (*handles)[descriptor.source];
        }
        if (descriptor.secondary != INVALID_TEXTURE) {
            if (descriptor.secondary >= handles->size()) return fail(error, "cache secondary texture source is not topologically ordered");
            descriptor.secondary = (*handles)[descriptor.secondary];
        }
        const TextureHandle handle = registerDeferredDescriptor(std::move(descriptor));
        if (handle == INVALID_TEXTURE) return fail(error, "failed to recreate cached texture source");
        handles->push_back(handle);
    }
    return true;
}

} // namespace

bool encode(
    const Formats::Document& document,
    const std::vector<DependencyStamp>& dependencies,
    std::vector<std::uint8_t> *bytes,
    std::vector<std::string> *texture_dependencies,
    std::string *error)
{
    if (error) error->clear();
    if (!bytes || !texture_dependencies) return fail(error, "null model cache encode output");

    TextureTable table;
    for (const Formats::Part& part : document.parts) collectMaterial(table, part.material);
    for (const Formats::VariantMaterial& variant : document.variant_materials) collectMaterial(table, variant.material);
    if (!table.ok) return fail(error, table.error);

    Writer out;
    writeVector(out, dependencies,
        [](Writer& writer, const DependencyStamp& value) { writeDependency(writer, value); });
    writeVector(out, table.descriptors,
        [](Writer& writer, const TextureSourceDescriptor& value) { writeDescriptor(writer, value); });

    if (!out.count(document.parts.size())) return fail(error, "model cache part count exceeds limit");
    for (const Formats::Part& part : document.parts) writePart(out, table, part);
    writeSkeleton(out, document.skeleton);
    writeVector(out, document.animations,
        [](Writer& writer, const Animation::AnimationClip& value) { writeClip(writer, value); });
    out.boolean(document.has_skeleton);
    writeVector(out, document.nodes, [](Writer& writer, const NodeData& value) { writeNode(writer, value); });
    writeVector(out, document.scenes, [](Writer& writer, const SceneData& value) { writeScene(writer, value); });
    out.u32(document.default_scene);
    writeVector(out, document.skins, [](Writer& writer, const SkinData& value) { writeSkin(writer, value); });
    writeVector(out, document.cameras, [](Writer& writer, const CameraData& value) { writeCamera(writer, value); });
    writeVector(out, document.lights, [](Writer& writer, const LightData& value) { writeLight(writer, value); });
    writeVector(out, document.model_animations,
        [](Writer& writer, const ModelAnimationData& value) { writeModelAnimation(writer, value); });
    writeVector(out, document.variants, [](Writer& writer, const MaterialVariantData& value) {
        writer.string(value.name);
    });
    if (!out.count(document.variant_materials.size())) return fail(error, "model cache variant material count exceeds limit");
    for (const Formats::VariantMaterial& variant : document.variant_materials)
        writeVariantMaterial(out, table, variant);
    writeVector(out, document.instances, [](Writer& writer, const InstanceData& value) { writeInstance(writer, value); });
    writeStringVector(out, document.dependencies);
    out.string(document.extras_json);
    writeStringMap(out, document.extensions_json);

    if (!out.good()) return fail(error, "model cache payload exceeds safety limits");
    *bytes = out.take();
    *texture_dependencies = std::move(table.file_dependencies);
    return true;
}

bool decode(
    const std::vector<std::uint8_t>& bytes,
    std::vector<DependencyStamp> *dependencies,
    Formats::Document *document,
    std::string *error)
{
    if (error) error->clear();
    if (!dependencies || !document) return fail(error, "null model cache decode output");

    Reader in(bytes);
    std::vector<TextureSourceDescriptor> descriptors;
    Formats::Document decoded;

    if (!readVector(in, dependencies,
            [](Reader& reader, DependencyStamp *value) { return readDependency(reader, value); }) ||
        !readVector(in, &descriptors,
            [](Reader& reader, TextureSourceDescriptor *value) { return readDescriptor(reader, value); }) ||
        !readVector(in, &decoded.parts, [](Reader& reader, Formats::Part *value) { return readPart(reader, value); }) ||
        !readSkeleton(in, &decoded.skeleton) ||
        !readVector(in, &decoded.animations,
            [](Reader& reader, Animation::AnimationClip *value) { return readClip(reader, value); }) ||
        !in.boolean(&decoded.has_skeleton) ||
        !readVector(in, &decoded.nodes, [](Reader& reader, NodeData *value) { return readNode(reader, value); }) ||
        !readVector(in, &decoded.scenes, [](Reader& reader, SceneData *value) { return readScene(reader, value); }) ||
        !in.u32(&decoded.default_scene) ||
        !readVector(in, &decoded.skins, [](Reader& reader, SkinData *value) { return readSkin(reader, value); }) ||
        !readVector(in, &decoded.cameras, [](Reader& reader, CameraData *value) { return readCamera(reader, value); }) ||
        !readVector(in, &decoded.lights, [](Reader& reader, LightData *value) { return readLight(reader, value); }) ||
        !readVector(in, &decoded.model_animations,
            [](Reader& reader, ModelAnimationData *value) { return readModelAnimation(reader, value); }) ||
        !readVector(in, &decoded.variants, [](Reader& reader, MaterialVariantData *value) {
            return reader.string(&value->name);
        }) ||
        !readVector(in, &decoded.variant_materials,
            [](Reader& reader, Formats::VariantMaterial *value) { return readVariantMaterial(reader, value); }) ||
        !readVector(in, &decoded.instances, [](Reader& reader, InstanceData *value) { return readInstance(reader, value); }) ||
        !readStringVector(in, &decoded.dependencies) || !in.string(&decoded.extras_json) ||
        !readStringMap(in, &decoded.extensions_json) || !in.finished())
        return fail(error, "invalid or truncated model cache payload");

    std::vector<TextureHandle> handles;
    if (!registerTextures(descriptors, &handles, error)) return false;

    bool remap_ok = true;
    for (Formats::Part& part : decoded.parts) remapMaterial(&part.material, handles, &remap_ok);
    for (Formats::VariantMaterial& variant : decoded.variant_materials)
        remapMaterial(&variant.material, handles, &remap_ok);
    if (!remap_ok) return fail(error, "model cache material references invalid texture id");

    *document = std::move(decoded);
    return true;
}

} // namespace Models::Internal::ModelCacheCodec
