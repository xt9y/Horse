#include "Models/Formats/GltfDraco.hpp"

#include "Models/Compression/Draco.hpp"
#include "Models/Formats/GltfData.hpp"
#include "Models/Formats/GltfFull.hpp"
#include "Models/Formats/GltfJson.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Models::Formats::GltfDraco {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

Value *member(Value *value, std::string_view name)
{
    if (!value || !value->is(Type::Object)) return nullptr;
    const auto found = value->object.find(std::string(name));
    return found == value->object.end() ? nullptr : &found->second;
}

const Value *member(const Value *value, std::string_view name)
{
    if (!value || !value->is(Type::Object)) return nullptr;
    const auto found = value->object.find(std::string(name));
    return found == value->object.end() ? nullptr : &found->second;
}

Value number(double value)
{
    Value result;
    result.type = Type::Number;
    result.number = value;
    return result;
}

Value string(std::string value)
{
    Value result;
    result.type = Type::String;
    result.string = std::move(value);
    return result;
}

Value object()
{
    Value result;
    result.type = Type::Object;
    return result;
}

void set(Value *value, std::string name, Value field)
{
    if (value && value->is(Type::Object)) value->object.insert_or_assign(std::move(name), std::move(field));
}

std::string base64(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    if (bytes.size() <= (std::numeric_limits<std::size_t>::max() - 2u) / 3u)
        out.reserve(((bytes.size() + 2u) / 3u) * 4u);
    for (std::size_t i = 0u; i < bytes.size(); i += 3u) {
        const std::uint32_t a = bytes[i];
        const std::uint32_t b = i + 1u < bytes.size() ? bytes[i + 1u] : 0u;
        const std::uint32_t c = i + 2u < bytes.size() ? bytes[i + 2u] : 0u;
        const std::uint32_t value = (a << 16u) | (b << 8u) | c;
        out.push_back(alphabet[(value >> 18u) & 63u]);
        out.push_back(alphabet[(value >> 12u) & 63u]);
        out.push_back(i + 1u < bytes.size() ? alphabet[(value >> 6u) & 63u] : '=');
        out.push_back(i + 2u < bytes.size() ? alphabet[value & 63u] : '=');
    }
    return out;
}

std::string dataUri(const std::vector<std::uint8_t>& bytes)
{
    return "data:application/octet-stream;base64," + base64(bytes);
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
    const auto& buffer = context.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.offset > buffer.size() || view.length > buffer.size() - view.offset)
        return fail(error, "Draco bufferView exceeds buffer bounds");
    *data = buffer.data() + view.offset;
    *size = view.length;
    return true;
}

void align4(std::vector<std::uint8_t> *bytes)
{
    if (!bytes) return;
    while ((bytes->size() & 3u) != 0u) bytes->push_back(0u);
}

template <typename T>
void appendScalar(std::vector<std::uint8_t> *bytes, T value)
{
    const std::size_t offset = bytes->size();
    bytes->resize(offset + sizeof(T));
    std::memcpy(bytes->data() + offset, &value, sizeof(T));
}

bool appendInteger(
    std::vector<std::uint8_t> *bytes,
    double source,
    int component_type,
    bool normalized,
    bool source_integral)
{
    double value = source;
    if (normalized && !source_integral) {
        switch (component_type) {
            case 5120: value = value <= -1.0 ? -128.0 : std::round(std::clamp(value, -1.0, 1.0) * 127.0); break;
            case 5121: value = std::round(std::clamp(value, 0.0, 1.0) * 255.0); break;
            case 5122: value = value <= -1.0 ? -32768.0 : std::round(std::clamp(value, -1.0, 1.0) * 32767.0); break;
            case 5123: value = std::round(std::clamp(value, 0.0, 1.0) * 65535.0); break;
            default: break;
        }
    } else {
        value = std::round(value);
    }

    switch (component_type) {
        case 5120:
            appendScalar(bytes, static_cast<std::int8_t>(std::clamp(value, -128.0, 127.0)));
            return true;
        case 5121:
            appendScalar(bytes, static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0)));
            return true;
        case 5122:
            appendScalar(bytes, static_cast<std::int16_t>(std::clamp(value, -32768.0, 32767.0)));
            return true;
        case 5123:
            appendScalar(bytes, static_cast<std::uint16_t>(std::clamp(value, 0.0, 65535.0)));
            return true;
        case 5125:
            appendScalar(bytes, static_cast<std::uint32_t>(std::clamp(value, 0.0, 4294967295.0)));
            return true;
        default:
            return false;
    }
}

bool encodeAttribute(
    const Compression::DracoAttribute& attribute,
    const GltfData::Accessor& accessor,
    std::vector<std::uint8_t> *bytes,
    std::string *error)
{
    if (!bytes) return false;
    const std::size_t components = GltfData::componentCount(accessor.type);
    if (components == 0u || attribute.components != components)
        return fail(error, "Draco attribute component count does not match glTF accessor");
    if (accessor.count > std::numeric_limits<std::size_t>::max() / components ||
        attribute.values.size() != accessor.count * components)
        return fail(error, "Draco attribute value count does not match glTF accessor");

    const bool source_integral = attribute.data_type != 9u && attribute.data_type != 10u;
    for (double value : attribute.values) {
        if (!std::isfinite(value)) return fail(error, "Draco attribute contains a non-finite value");
        if (accessor.component_type == 5126) {
            appendScalar(bytes, static_cast<float>(value));
        } else if (!appendInteger(bytes, value, accessor.component_type, accessor.normalized, source_integral)) {
            return fail(error, "unsupported glTF accessor component type for Draco attribute");
        }
    }
    return true;
}

bool encodeIndices(
    const Compression::DracoMesh& mesh,
    int component_type,
    std::vector<std::uint8_t> *bytes,
    std::string *error)
{
    if (!bytes) return false;
    for (const std::uint32_t index : mesh.indices) {
        switch (component_type) {
            case 5121:
                if (index > UINT8_MAX) return fail(error, "Draco index exceeds glTF UBYTE accessor");
                appendScalar(bytes, static_cast<std::uint8_t>(index));
                break;
            case 5123:
                if (index > UINT16_MAX) return fail(error, "Draco index exceeds glTF USHORT accessor");
                appendScalar(bytes, static_cast<std::uint16_t>(index));
                break;
            case 5125:
                appendScalar(bytes, index);
                break;
            default:
                return fail(error, "Draco index accessor must use an unsigned integer component type");
        }
    }
    return true;
}

int appendView(Value *root, int buffer, std::size_t offset, std::size_t length)
{
    Value *views = member(root, "bufferViews");
    if (!views) {
        Value array;
        array.type = Type::Array;
        set(root, "bufferViews", std::move(array));
        views = member(root, "bufferViews");
    }
    if (!views || !views->is(Type::Array)) return -1;

    Value view = object();
    set(&view, "buffer", number(buffer));
    set(&view, "byteOffset", number(static_cast<double>(offset)));
    set(&view, "byteLength", number(static_cast<double>(length)));
    views->array.push_back(std::move(view));
    return static_cast<int>(views->array.size() - 1u);
}

bool materializeAccessor(
    Value *root,
    int accessor_index,
    int buffer,
    std::vector<std::uint8_t> *synthetic,
    const std::vector<std::uint8_t>& encoded,
    std::string *error)
{
    Value *accessors = member(root, "accessors");
    if (!accessors || !accessors->is(Type::Array) || accessor_index < 0 ||
        static_cast<std::size_t>(accessor_index) >= accessors->array.size())
        return fail(error, "Draco primitive references invalid glTF accessor");

    align4(synthetic);
    const std::size_t offset = synthetic->size();
    synthetic->insert(synthetic->end(), encoded.begin(), encoded.end());
    const int view = appendView(root, buffer, offset, encoded.size());
    if (view < 0) return fail(error, "failed to append Draco bufferView");

    Value& accessor = accessors->array[static_cast<std::size_t>(accessor_index)];
    if (!accessor.is(Type::Object)) return fail(error, "invalid glTF accessor object");
    set(&accessor, "bufferView", number(view));
    set(&accessor, "byteOffset", number(0));
    accessor.object.erase("sparse");
    return true;
}

bool addIndexAccessor(
    Value *root,
    int buffer,
    std::vector<std::uint8_t> *synthetic,
    const Compression::DracoMesh& mesh,
    int *index,
    std::string *error)
{
    if (!index) return false;
    Value *accessors = member(root, "accessors");
    if (!accessors) {
        Value array;
        array.type = Type::Array;
        set(root, "accessors", std::move(array));
        accessors = member(root, "accessors");
    }
    if (!accessors || !accessors->is(Type::Array)) return fail(error, "glTF accessors must be an array");

    std::vector<std::uint8_t> encoded;
    if (!encodeIndices(mesh, 5125, &encoded, error)) return false;
    align4(synthetic);
    const std::size_t offset = synthetic->size();
    synthetic->insert(synthetic->end(), encoded.begin(), encoded.end());
    const int view = appendView(root, buffer, offset, encoded.size());
    if (view < 0) return fail(error, "failed to append Draco index bufferView");

    Value accessor = object();
    set(&accessor, "bufferView", number(view));
    set(&accessor, "byteOffset", number(0));
    set(&accessor, "componentType", number(5125));
    set(&accessor, "count", number(static_cast<double>(mesh.indices.size())));
    set(&accessor, "type", string("SCALAR"));
    accessors->array.push_back(std::move(accessor));
    *index = static_cast<int>(accessors->array.size() - 1u);
    return true;
}

bool decodePrimitive(
    Value *root,
    Value *primitive,
    const GltfData::Context& data,
    int synthetic_buffer,
    std::vector<std::uint8_t> *synthetic,
    std::string *error)
{
    Value *extensions = member(primitive, "extensions");
    Value *draco = member(extensions, "KHR_draco_mesh_compression");
    if (!draco) return true;
    if (!draco->is(Type::Object)) return fail(error, "KHR_draco_mesh_compression must be an object");

    const int mode = GltfJson::integer(member(primitive, "mode"), 4);
    if (mode != 4 && mode != 5)
        return fail(error, "KHR_draco_mesh_compression only supports TRIANGLES or TRIANGLE_STRIP");

    const int view = GltfJson::integer(member(draco, "bufferView"));
    const std::uint8_t *compressed = nullptr;
    std::size_t compressed_size = 0u;
    if (!rawView(data, view, &compressed, &compressed_size, error)) return false;

    Compression::DracoMesh mesh;
    if (!Compression::decodeDraco(compressed, compressed_size, &mesh, error)) return false;
    if (mesh.point_count == 0u || mesh.indices.empty()) return fail(error, "Draco primitive decoded to empty geometry");

    int index_accessor = GltfJson::integer(member(primitive, "indices"));
    Value *accessors = member(root, "accessors");
    if (index_accessor >= 0) {
        if (!accessors || !accessors->is(Type::Array) ||
            static_cast<std::size_t>(index_accessor) >= data.accessors.size())
            return fail(error, "Draco primitive references invalid index accessor");
        const GltfData::Accessor& accessor = data.accessors[static_cast<std::size_t>(index_accessor)];
        if (accessor.type != "SCALAR") return fail(error, "Draco index accessor must be SCALAR");
        if (mode == 4 && accessor.count != mesh.indices.size())
            return fail(error, "Draco index count does not match glTF accessor");
        std::vector<std::uint8_t> encoded;
        if (!encodeIndices(mesh, accessor.component_type, &encoded, error) ||
            !materializeAccessor(root, index_accessor, synthetic_buffer, synthetic, encoded, error))
            return false;
        if (mode == 5) {
            Value& json_accessor = accessors->array[static_cast<std::size_t>(index_accessor)];
            set(&json_accessor, "count", number(static_cast<double>(mesh.indices.size())));
        }
    } else {
        if (!addIndexAccessor(root, synthetic_buffer, synthetic, mesh, &index_accessor, error)) return false;
        set(primitive, "indices", number(index_accessor));
    }

    Value *primitive_attributes = member(primitive, "attributes");
    Value *draco_attributes = member(draco, "attributes");
    if (!primitive_attributes || !primitive_attributes->is(Type::Object) ||
        !draco_attributes || !draco_attributes->is(Type::Object))
        return fail(error, "Draco primitive attributes are invalid");

    for (const auto& [semantic, unique_value] : draco_attributes->object) {
        if (!unique_value.is(Type::Number)) return fail(error, "Draco attribute id must be numeric");
        const Value *accessor_value = member(primitive_attributes, semantic);
        const int accessor_index = GltfJson::integer(accessor_value);
        if (accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= data.accessors.size())
            return fail(error, "Draco attribute references invalid glTF accessor: " + semantic);
        const std::uint32_t unique_id = GltfJson::unsignedInteger(&unique_value);
        const Compression::DracoAttribute *attribute = mesh.attribute(unique_id);
        if (!attribute) return fail(error, "Draco stream is missing requested attribute: " + semantic);
        const GltfData::Accessor& accessor = data.accessors[static_cast<std::size_t>(accessor_index)];
        std::vector<std::uint8_t> encoded;
        if (!encodeAttribute(*attribute, accessor, &encoded, error) ||
            !materializeAccessor(root, accessor_index, synthetic_buffer, synthetic, encoded, error))
            return false;
    }

    if (mode == 5) set(primitive, "mode", number(4));
    extensions->object.erase("KHR_draco_mesh_compression");
    return true;
}

bool decodePrimitives(
    Value *root,
    const GltfData::Context& data,
    int synthetic_buffer,
    std::vector<std::uint8_t> *synthetic,
    bool *changed,
    std::string *error)
{
    if (!changed) return false;
    *changed = false;
    Value *meshes = member(root, "meshes");
    if (!meshes) return true;
    if (!meshes->is(Type::Array)) return fail(error, "glTF meshes must be an array");
    for (Value& mesh : meshes->array) {
        Value *primitives = member(&mesh, "primitives");
        if (!primitives || !primitives->is(Type::Array)) return fail(error, "glTF mesh primitives must be an array");
        for (Value& primitive : primitives->array) {
            Value *extensions = member(&primitive, "extensions");
            if (!member(extensions, "KHR_draco_mesh_compression")) continue;
            if (!decodePrimitive(root, &primitive, data, synthetic_buffer, synthetic, error)) return false;
            *changed = true;
        }
    }
    return true;
}

bool embedBuffers(Value *root, const GltfData::Context& data, std::string *error)
{
    Value *buffers = member(root, "buffers");
    if (!buffers) return true;
    if (!buffers->is(Type::Array) || buffers->array.size() > data.buffers.size())
        return fail(error, "invalid glTF buffers while materializing Draco");
    for (std::size_t i = 0u; i < buffers->array.size(); ++i) {
        Value& buffer = buffers->array[i];
        if (!buffer.is(Type::Object)) return fail(error, "invalid glTF buffer object");
        set(&buffer, "uri", string(dataUri(data.buffers[i])));
        set(&buffer, "byteLength", number(static_cast<double>(data.buffers[i].size())));
    }
    return true;
}

bool rewriteImages(Value *root, const std::filesystem::path& directory, std::string *error)
{
    Value *images = member(root, "images");
    if (!images) return true;
    if (!images->is(Type::Array)) return fail(error, "glTF images must be an array");
    for (Value& image : images->array) {
        Value *uri = member(&image, "uri");
        if (!uri) continue;
        if (!uri->is(Type::String)) return fail(error, "glTF image URI must be a string");
        if (uri->string.starts_with("data:")) continue;
        std::string decoded;
        if (!GltfData::decodeUriPath(uri->string, &decoded, error)) return false;
        std::filesystem::path source(decoded);
        if (source.is_relative()) source = (directory / source).lexically_normal();
        uri->string = source.generic_string();
    }
    return true;
}

bool supportedCodecRequired(std::string_view name)
{
    return name == "KHR_draco_mesh_compression" ||
        name == "EXT_meshopt_compression" ||
        name == "KHR_texture_basisu" ||
        name == "EXT_texture_webp";
}

void filterRequired(Value *root)
{
    Value *required = member(root, "extensionsRequired");
    if (!required || !required->is(Type::Array)) return;
    std::erase_if(required->array, [](const Value& value) {
        return value.is(Type::String) && supportedCodecRequired(value.string);
    });
}

void removeDracoUsed(Value *root)
{
    Value *used = member(root, "extensionsUsed");
    if (!used || !used->is(Type::Array)) return;
    std::erase_if(used->array, [](const Value& value) {
        return value.is(Type::String) && value.string == "KHR_draco_mesh_compression";
    });
}

bool needsPreprocess(const Value& root)
{
    const Value *used = member(&root, "extensionsUsed");
    if (used && used->is(Type::Array)) {
        for (const Value& value : used->array)
            if (value.is(Type::String) && value.string == "KHR_draco_mesh_compression") return true;
    }
    const Value *required = member(&root, "extensionsRequired");
    if (required && required->is(Type::Array)) {
        for (const Value& value : required->array)
            if (value.is(Type::String) && supportedCodecRequired(value.string)) return true;
    }
    return false;
}

class TemporaryFile {
public:
    explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

bool writeTemporary(const std::string& source, const std::string& json, std::filesystem::path *out, std::string *error)
{
    if (!out) return false;
    std::error_code ec;
    std::filesystem::path directory = std::filesystem::temp_directory_path(ec);
    if (ec) directory = std::filesystem::path(source).parent_path();
    static std::atomic<std::uint64_t> sequence {0u};
    const std::uint64_t id = sequence.fetch_add(1u, std::memory_order_relaxed);
    const std::size_t hash = std::hash<std::string>{}(source);
    *out = directory / ("horse-gltf-" + std::to_string(hash) + "-" + std::to_string(id) + ".gltf");
    std::ofstream file(*out, std::ios::binary | std::ios::trunc);
    if (!file) return fail(error, "failed to create temporary decompressed glTF");
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!file) return fail(error, "failed to write temporary decompressed glTF");
    return true;
}

} // namespace

bool load(const std::string& path, Document *output, std::string *error)
{
    if (error) error->clear();
    if (!output) return fail(error, "null glTF output");

    std::string json;
    std::vector<std::uint8_t> binary;
    if (!GltfData::parseContainer(path, &json, &binary, error)) return false;

    Value root;
    if (!GltfJson::parse(json, &root, error)) return false;
    if (!root.is(Type::Object)) return fail(error, "glTF root must be an object");
    if (!needsPreprocess(root)) return GltfFull::load(path, output, error);

    GltfData::Context data;
    data.path = path;
    data.directory = std::filesystem::path(path).parent_path();
    data.root = &root;
    if (!GltfData::loadBuffers(&data, binary, error) ||
        !GltfData::loadViews(&data, error) ||
        !GltfData::loadAccessors(&data, error))
        return false;

    Value *buffers = member(&root, "buffers");
    if (!buffers || !buffers->is(Type::Array)) return fail(error, "compressed glTF has no buffers array");
    const int synthetic_buffer = static_cast<int>(buffers->array.size());
    std::vector<std::uint8_t> synthetic;
    bool decoded_draco = false;
    if (!decodePrimitives(&root, data, synthetic_buffer, &synthetic, &decoded_draco, error)) return false;
    if (!embedBuffers(&root, data, error) || !rewriteImages(&root, data.directory, error)) return false;

    if (decoded_draco) {
        Value buffer = object();
        set(&buffer, "byteLength", number(static_cast<double>(synthetic.size())));
        set(&buffer, "uri", string(dataUri(synthetic)));
        buffers = member(&root, "buffers");
        if (!buffers || !buffers->is(Type::Array)) return fail(error, "glTF buffers disappeared during Draco preprocessing");
        buffers->array.push_back(std::move(buffer));
        removeDracoUsed(&root);
    }
    filterRequired(&root);

    const std::string materialized = GltfJson::stringify(root);
    std::filesystem::path temporary_path;
    if (!writeTemporary(path, materialized, &temporary_path, error)) return false;
    TemporaryFile temporary(std::move(temporary_path));
    return GltfFull::load(temporary.path().string(), output, error);
}

} // namespace Models::Formats::GltfDraco
