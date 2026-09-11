#include "Models/Formats/GltfData.hpp"

#include "Models/Compression/Meshopt.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace Models::Formats::GltfData {
namespace {

using GltfJson::Type;
using GltfJson::Value;

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint32_t u32le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

int base64Digit(unsigned char value)
{
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return 26 + value - 'a';
    if (value >= '0' && value <= '9') return 52 + value - '0';
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

bool base64(std::string_view input, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!out) return fail(error, "base64 output is null");
    out->clear();
    std::uint32_t accumulator = 0u;
    int bits = 0;
    int padding = 0;
    for (const unsigned char value : input) {
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') continue;
        if (value == '=') {
            ++padding;
            if (padding > 2) return fail(error, "invalid base64 padding in glTF data URI");
            continue;
        }
        if (padding != 0) return fail(error, "base64 data appears after padding");
        const int digit = base64Digit(value);
        if (digit < 0) return fail(error, "invalid base64 character in glTF data URI");
        accumulator = (accumulator << 6u) | static_cast<std::uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<std::uint8_t>((accumulator >> static_cast<unsigned int>(bits)) & 0xffu));
        }
    }
    if (bits >= 6) return fail(error, "invalid base64 length in glTF data URI");
    return true;
}

int hexDigit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return 10 + value - 'a';
    if (value >= 'A' && value <= 'F') return 10 + value - 'A';
    return -1;
}

bool percentDecode(std::string_view input, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!out) return fail(error, "URI decode output is null");
    out->clear();
    out->reserve(input.size());
    for (std::size_t index = 0u; index < input.size(); ++index) {
        if (input[index] != '%') {
            out->push_back(static_cast<std::uint8_t>(input[index]));
            continue;
        }
        if (index + 2u >= input.size()) return fail(error, "truncated percent escape in glTF URI");
        const int high = hexDigit(input[index + 1u]);
        const int low = hexDigit(input[index + 2u]);
        if (high < 0 || low < 0) return fail(error, "invalid percent escape in glTF URI");
        out->push_back(static_cast<std::uint8_t>((high << 4) | low));
        index += 2u;
    }
    return true;
}

struct Layout {
    std::size_t components = 0u;
    std::size_t columns = 1u;
    std::size_t rows = 1u;
    std::size_t component_size = 0u;
    std::size_t column_stride = 0u;
    std::size_t element_size = 0u;
};

std::size_t align4(std::size_t value)
{
    return (value + 3u) & ~std::size_t{3u};
}

Layout layout(std::string_view type, int component_type)
{
    Layout result;
    result.component_size = componentSize(component_type);
    if (type == "SCALAR") {
        result.components = 1u;
        result.rows = 1u;
    } else if (type == "VEC2") {
        result.components = 2u;
        result.rows = 2u;
    } else if (type == "VEC3") {
        result.components = 3u;
        result.rows = 3u;
    } else if (type == "VEC4") {
        result.components = 4u;
        result.rows = 4u;
    } else if (type == "MAT2") {
        result.components = 4u;
        result.columns = 2u;
        result.rows = 2u;
    } else if (type == "MAT3") {
        result.components = 9u;
        result.columns = 3u;
        result.rows = 3u;
    } else if (type == "MAT4") {
        result.components = 16u;
        result.columns = 4u;
        result.rows = 4u;
    }
    if (result.components == 0u || result.component_size == 0u) return {};

    const std::size_t raw_column = result.rows * result.component_size;
    result.column_stride = result.columns > 1u && result.component_size < 4u
        ? align4(raw_column)
        : raw_column;
    result.element_size = result.columns * result.column_stride;
    return result;
}

std::size_t componentOffset(const Layout& value, std::size_t component)
{
    const std::size_t column = component / value.rows;
    const std::size_t row = component % value.rows;
    return column * value.column_stride + row * value.component_size;
}

bool range(
    const Context& context,
    int view_index,
    std::size_t relative_offset,
    std::size_t bytes,
    const std::uint8_t **out,
    std::string *error)
{
    if (!out || view_index < 0 || static_cast<std::size_t>(view_index) >= context.views.size())
        return fail(error, "glTF references invalid bufferView");
    const BufferView& view = context.views[static_cast<std::size_t>(view_index)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= context.buffers.size())
        return fail(error, "glTF bufferView references invalid buffer");
    if (relative_offset > view.length || bytes > view.length - relative_offset)
        return fail(error, "glTF accessor exceeds bufferView range");
    const std::vector<std::uint8_t>& buffer = context.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.offset > buffer.size() || relative_offset > buffer.size() - view.offset ||
        bytes > buffer.size() - view.offset - relative_offset)
        return fail(error, "glTF bufferView exceeds buffer range");
    *out = buffer.data() + view.offset + relative_offset;
    return true;
}

template <typename T>
T scalar(const std::uint8_t *data)
{
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

float asFloat(const std::uint8_t *data, int type, bool normalized)
{
    switch (type) {
        case 5120: {
            const std::int8_t value = scalar<std::int8_t>(data);
            return normalized ? std::max(-1.0f, static_cast<float>(value) / 127.0f) : static_cast<float>(value);
        }
        case 5121: {
            const std::uint8_t value = scalar<std::uint8_t>(data);
            return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
        }
        case 5122: {
            const std::int16_t value = scalar<std::int16_t>(data);
            return normalized ? std::max(-1.0f, static_cast<float>(value) / 32767.0f) : static_cast<float>(value);
        }
        case 5123: {
            const std::uint16_t value = scalar<std::uint16_t>(data);
            return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
        }
        case 5125: {
            const std::uint32_t value = scalar<std::uint32_t>(data);
            return normalized
                ? static_cast<float>(static_cast<double>(value) / 4294967295.0)
                : static_cast<float>(value);
        }
        case 5126: return scalar<float>(data);
        default: return 0.0f;
    }
}

bool asUnsigned(const std::uint8_t *data, int type, std::uint32_t *out)
{
    if (!out) return false;
    switch (type) {
        case 5121: *out = scalar<std::uint8_t>(data); return true;
        case 5123: *out = scalar<std::uint16_t>(data); return true;
        case 5125: *out = scalar<std::uint32_t>(data); return true;
        default: return false;
    }
}

bool requiredBytes(std::size_t count, std::size_t stride, std::size_t element, std::size_t *out)
{
    if (!out) return false;
    if (count == 0u) {
        *out = 0u;
        return true;
    }
    if (stride == 0u || count - 1u > (std::numeric_limits<std::size_t>::max() - element) / stride)
        return false;
    *out = (count - 1u) * stride + element;
    return true;
}

bool sparseRanges(
    const Context& context,
    const Accessor& accessor,
    const Layout& value_layout,
    const std::uint8_t **indices,
    std::size_t *index_size,
    const std::uint8_t **values,
    std::string *error)
{
    if (!indices || !index_size || !values) return false;
    *index_size = componentSize(accessor.sparse_indices_component);
    if (*index_size == 0u) return fail(error, "invalid sparse accessor index component type");
    if (accessor.sparse_count > std::numeric_limits<std::size_t>::max() / *index_size)
        return fail(error, "sparse accessor index range overflows");
    if (accessor.sparse_count > std::numeric_limits<std::size_t>::max() / value_layout.element_size)
        return fail(error, "sparse accessor value range overflows");
    return range(
        context,
        accessor.sparse_indices_view,
        accessor.sparse_indices_offset,
        accessor.sparse_count * *index_size,
        indices,
        error
    ) && range(
        context,
        accessor.sparse_values_view,
        accessor.sparse_values_offset,
        accessor.sparse_count * value_layout.element_size,
        values,
        error
    );
}

bool multiplication(std::size_t a, std::size_t b, std::size_t *out)
{
    if (!out) return false;
    if (a != 0u && b > std::numeric_limits<std::size_t>::max() / a) return false;
    *out = a * b;
    return true;
}

bool meshoptMode(std::string_view name, Compression::MeshoptMode *out)
{
    if (!out) return false;
    if (name == "ATTRIBUTES") *out = Compression::MeshoptMode::Attributes;
    else if (name == "TRIANGLES") *out = Compression::MeshoptMode::Triangles;
    else if (name == "INDICES") *out = Compression::MeshoptMode::Indices;
    else return false;
    return true;
}

bool meshoptFilter(std::string_view name, Compression::MeshoptFilter *out)
{
    if (!out) return false;
    if (name.empty() || name == "NONE") *out = Compression::MeshoptFilter::None;
    else if (name == "OCTAHEDRAL") *out = Compression::MeshoptFilter::Octahedral;
    else if (name == "QUATERNION") *out = Compression::MeshoptFilter::Quaternion;
    else if (name == "EXPONENTIAL") *out = Compression::MeshoptFilter::Exponential;
    else return false;
    return true;
}

const Value *meshoptExtension(const Value& source)
{
    const Value *extensions = source.get("extensions");
    return extensions && extensions->is(Type::Object)
        ? extensions->get("EXT_meshopt_compression")
        : nullptr;
}

} // namespace

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!out) return fail(error, "file output is null");
    std::ifstream input(path, std::ios::binary);
    if (!input) return fail(error, "failed to open glTF resource: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max())
        return fail(error, "failed to size glTF resource: " + path.string());
    input.seekg(0, std::ios::beg);
    out->resize(static_cast<std::size_t>(length));
    if (!out->empty())
        input.read(reinterpret_cast<char *>(out->data()), static_cast<std::streamsize>(out->size()));
    if (!input && !out->empty()) return fail(error, "failed to read glTF resource: " + path.string());
    return true;
}

bool parseContainer(
    const std::string& path,
    std::string *json,
    std::vector<std::uint8_t> *binary,
    std::string *error)
{
    if (error) error->clear();
    if (!json || !binary) return fail(error, "glTF container output is null");
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, &bytes, error)) return false;
    binary->clear();

    if (bytes.size() < 4u || u32le(bytes.data()) != 0x46546c67u) {
        std::size_t offset = 0u;
        if (bytes.size() >= 3u && bytes[0] == 0xefu && bytes[1] == 0xbbu && bytes[2] == 0xbfu)
            offset = 3u;
        json->assign(reinterpret_cast<const char *>(bytes.data() + offset), bytes.size() - offset);
        return true;
    }

    if (bytes.size() < 12u) return fail(error, "truncated GLB header");
    const std::uint32_t version = u32le(bytes.data() + 4u);
    const std::uint32_t declared = u32le(bytes.data() + 8u);
    if (version != 2u) return fail(error, "unsupported GLB version: " + std::to_string(version));
    if (declared != bytes.size()) return fail(error, "GLB declared length does not match file length");

    bool have_json = false;
    std::size_t cursor = 12u;
    std::size_t chunk_index = 0u;
    while (cursor < declared) {
        if (declared - cursor < 8u) return fail(error, "truncated GLB chunk header");
        const std::uint32_t length = u32le(bytes.data() + cursor);
        const std::uint32_t type = u32le(bytes.data() + cursor + 4u);
        cursor += 8u;
        if (length > declared - cursor) return fail(error, "GLB chunk exceeds container length");
        if (chunk_index == 0u && type != 0x4e4f534au)
            return fail(error, "first GLB chunk is not JSON");
        if (type == 0x4e4f534au) {
            if (have_json) return fail(error, "GLB contains multiple JSON chunks");
            json->assign(reinterpret_cast<const char *>(bytes.data() + cursor), length);
            while (!json->empty() &&
                (json->back() == '\0' || json->back() == ' ' || json->back() == '\t' ||
                 json->back() == '\r' || json->back() == '\n'))
                json->pop_back();
            have_json = true;
        } else if (type == 0x004e4942u && binary->empty()) {
            binary->assign(
                bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                bytes.begin() + static_cast<std::ptrdiff_t>(cursor + length)
            );
        }
        cursor += length;
        ++chunk_index;
    }
    if (!have_json) return fail(error, "GLB has no JSON chunk");
    return true;
}

bool decodeDataUri(std::string_view uri, std::vector<std::uint8_t> *out, std::string *error)
{
    if (!uri.starts_with("data:")) return fail(error, "URI is not a data URI");
    const std::size_t comma = uri.find(',');
    if (comma == std::string_view::npos) return fail(error, "invalid glTF data URI");
    const std::string_view metadata = uri.substr(5u, comma - 5u);
    const std::string_view payload = uri.substr(comma + 1u);
    return metadata.find(";base64") != std::string_view::npos
        ? base64(payload, out, error)
        : percentDecode(payload, out, error);
}

bool decodeUriPath(std::string_view uri, std::string *out, std::string *error)
{
    if (!out) return fail(error, "URI path output is null");
    const std::size_t colon = uri.find(':');
    const std::size_t slash = uri.find('/');
    if (colon != std::string_view::npos && (slash == std::string_view::npos || colon < slash))
        return fail(error, "unsupported external glTF URI scheme: " + std::string(uri.substr(0u, colon)));
    std::vector<std::uint8_t> decoded;
    if (!percentDecode(uri, &decoded, error)) return false;
    out->assign(reinterpret_cast<const char *>(decoded.data()), decoded.size());
    return true;
}

bool loadBuffers(Context *context, const std::vector<std::uint8_t>& binary, std::string *error)
{
    if (!context || !context->root) return fail(error, "glTF buffer context is null");
    const Value *buffers = context->root->get("buffers");
    if (!buffers) return true;
    if (!buffers->is(Type::Array)) return fail(error, "glTF buffers must be an array");
    context->buffers.clear();
    context->buffers.reserve(buffers->array.size());
    bool used_binary = false;

    for (const Value& source : buffers->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF buffer object");
        const std::size_t declared = GltfJson::sizeValue(source.get("byteLength"));
        if (declared == 0u) return fail(error, "glTF buffer byteLength must be nonzero");
        std::vector<std::uint8_t> bytes;
        bool available = true;
        const Value *uri = source.get("uri");
        if (!uri) {
            if (!used_binary && !binary.empty()) {
                bytes = binary;
                used_binary = true;
            } else {
                available = false;
            }
        } else if (!uri->is(Type::String)) {
            return fail(error, "glTF buffer URI must be a string");
        } else if (uri->string.starts_with("data:")) {
            if (!decodeDataUri(uri->string, &bytes, error)) return false;
        } else {
            std::string decoded;
            if (!decodeUriPath(uri->string, &decoded, error)) return false;
            if (!readFile(context->directory / std::filesystem::path(decoded), &bytes, error)) return false;
        }
        if (available && bytes.size() < declared)
            return fail(error, "glTF buffer is shorter than declared byteLength");
        context->buffers.push_back(std::move(bytes));
    }
    return true;
}

bool loadViews(Context *context, std::string *error)
{
    if (!context || !context->root) return fail(error, "glTF bufferView context is null");
    const Value *views = context->root->get("bufferViews");
    if (!views) return true;
    if (!views->is(Type::Array)) return fail(error, "glTF bufferViews must be an array");
    context->views.clear();
    context->views.reserve(views->array.size());

    for (const Value& source : views->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF bufferView object");
        BufferView view;
        view.buffer = GltfJson::integer(source.get("buffer"));
        view.offset = GltfJson::sizeValue(source.get("byteOffset"));
        view.length = GltfJson::sizeValue(source.get("byteLength"));
        view.stride = GltfJson::sizeValue(source.get("byteStride"));
        view.target = GltfJson::integer(source.get("target"), 0);
        if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= context->buffers.size())
            return fail(error, "glTF bufferView references invalid buffer");
        if (view.length == 0u) return fail(error, "glTF bufferView byteLength must be nonzero");

        const Value *meshopt = meshoptExtension(source);
        if (meshopt) {
            if (!meshopt->is(Type::Object)) return fail(error, "EXT_meshopt_compression must be an object");
            const int compressed_buffer = GltfJson::integer(meshopt->get("buffer"));
            const std::size_t compressed_offset = GltfJson::sizeValue(meshopt->get("byteOffset"));
            const std::size_t compressed_length = GltfJson::sizeValue(meshopt->get("byteLength"));
            const std::size_t compressed_stride = GltfJson::sizeValue(meshopt->get("byteStride"));
            const std::size_t count = GltfJson::sizeValue(meshopt->get("count"));
            const std::string mode_name = GltfJson::stringValue(meshopt->get("mode"));
            const std::string filter_name = GltfJson::stringValue(meshopt->get("filter"), "NONE");
            if (compressed_buffer < 0 || static_cast<std::size_t>(compressed_buffer) >= context->buffers.size())
                return fail(error, "EXT_meshopt_compression references invalid buffer");
            if (compressed_length == 0u || compressed_stride == 0u || count == 0u)
                return fail(error, "EXT_meshopt_compression has invalid byteLength, byteStride, or count");

            Compression::MeshoptMode mode{};
            Compression::MeshoptFilter filter{};
            if (!meshoptMode(mode_name, &mode)) return fail(error, "EXT_meshopt_compression has unsupported mode: " + mode_name);
            if (!meshoptFilter(filter_name, &filter)) return fail(error, "EXT_meshopt_compression has unsupported filter: " + filter_name);
            if (mode == Compression::MeshoptMode::Attributes) {
                if ((compressed_stride % 4u) != 0u || compressed_stride > 256u)
                    return fail(error, "EXT_meshopt_compression ATTRIBUTES has invalid byteStride");
            } else if (compressed_stride != 2u && compressed_stride != 4u) {
                return fail(error, "EXT_meshopt_compression index mode requires byteStride 2 or 4");
            }
            if (mode == Compression::MeshoptMode::Triangles && (count % 3u) != 0u)
                return fail(error, "EXT_meshopt_compression TRIANGLES count is not divisible by three");
            if (mode != Compression::MeshoptMode::Attributes && filter != Compression::MeshoptFilter::None)
                return fail(error, "EXT_meshopt_compression index modes cannot use filters");
            if (filter == Compression::MeshoptFilter::Octahedral && compressed_stride != 4u && compressed_stride != 8u)
                return fail(error, "EXT_meshopt_compression OCTAHEDRAL requires byteStride 4 or 8");
            if (filter == Compression::MeshoptFilter::Quaternion && compressed_stride != 8u)
                return fail(error, "EXT_meshopt_compression QUATERNION requires byteStride 8");
            if (filter == Compression::MeshoptFilter::Exponential && (compressed_stride % 4u) != 0u)
                return fail(error, "EXT_meshopt_compression EXPONENTIAL requires byteStride divisible by four");
            if (view.stride != 0u && view.stride != compressed_stride)
                return fail(error, "EXT_meshopt_compression byteStride does not match parent bufferView");

            std::size_t expected_length = 0u;
            if (!multiplication(count, compressed_stride, &expected_length) || expected_length != view.length)
                return fail(error, "EXT_meshopt_compression decompressed size does not match parent bufferView");
            const auto& compressed = context->buffers[static_cast<std::size_t>(compressed_buffer)];
            if (compressed_offset > compressed.size() || compressed_length > compressed.size() - compressed_offset)
                return fail(error, "EXT_meshopt_compression source range exceeds buffer bounds");

            std::vector<std::uint8_t> decoded;
            if (!Compression::decodeMeshopt(
                    compressed.data() + compressed_offset,
                    compressed_length,
                    count,
                    compressed_stride,
                    mode,
                    filter,
                    &decoded,
                    error))
                return false;
            if (decoded.size() != expected_length)
                return fail(error, "EXT_meshopt_compression decoder returned unexpected output size");
            if (context->buffers.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return fail(error, "too many synthetic glTF buffers after Meshopt decompression");
            view.buffer = static_cast<int>(context->buffers.size());
            view.offset = 0u;
            view.length = decoded.size();
            view.stride = compressed_stride;
            context->buffers.push_back(std::move(decoded));
        } else {
            if (view.stride != 0u && (view.stride < 4u || view.stride > 252u || (view.stride % 4u) != 0u))
                return fail(error, "invalid glTF bufferView byteStride");
            const auto& buffer = context->buffers[static_cast<std::size_t>(view.buffer)];
            if (view.offset > buffer.size() || view.length > buffer.size() - view.offset)
                return fail(error, "glTF bufferView exceeds buffer bounds or references an unavailable fallback buffer");
        }
        context->views.push_back(view);
    }
    return true;
}

std::size_t componentSize(int component_type)
{
    switch (component_type) {
        case 5120:
        case 5121: return 1u;
        case 5122:
        case 5123: return 2u;
        case 5125:
        case 5126: return 4u;
        default: return 0u;
    }
}

std::size_t componentCount(std::string_view type)
{
    if (type == "SCALAR") return 1u;
    if (type == "VEC2") return 2u;
    if (type == "VEC3") return 3u;
    if (type == "VEC4" || type == "MAT2") return 4u;
    if (type == "MAT3") return 9u;
    if (type == "MAT4") return 16u;
    return 0u;
}

bool loadAccessors(Context *context, std::string *error)
{
    if (!context || !context->root) return fail(error, "glTF accessor context is null");
    const Value *accessors = context->root->get("accessors");
    if (!accessors) return true;
    if (!accessors->is(Type::Array)) return fail(error, "glTF accessors must be an array");
    context->accessors.clear();
    context->accessors.reserve(accessors->array.size());

    for (const Value& source : accessors->array) {
        if (!source.is(Type::Object)) return fail(error, "invalid glTF accessor object");
        Accessor accessor;
        accessor.view = GltfJson::integer(source.get("bufferView"));
        accessor.offset = GltfJson::sizeValue(source.get("byteOffset"));
        accessor.count = GltfJson::sizeValue(source.get("count"));
        accessor.component_type = GltfJson::integer(source.get("componentType"), 0);
        accessor.type = GltfJson::stringValue(source.get("type"));
        accessor.normalized = GltfJson::boolValue(source.get("normalized"));
        const Layout value_layout = layout(accessor.type, accessor.component_type);
        if (accessor.count == 0u || value_layout.element_size == 0u)
            return fail(error, "invalid glTF accessor layout");
        if (accessor.view >= 0 && static_cast<std::size_t>(accessor.view) >= context->views.size())
            return fail(error, "glTF accessor references invalid bufferView");
        if (accessor.view < 0 && !source.get("sparse"))
            return fail(error, "glTF accessor has neither bufferView nor sparse values");

        const Value *sparse = source.get("sparse");
        if (sparse) {
            if (!sparse->is(Type::Object)) return fail(error, "glTF sparse accessor must be an object");
            accessor.sparse_count = GltfJson::sizeValue(sparse->get("count"));
            const Value *indices = sparse->get("indices");
            const Value *values = sparse->get("values");
            if (accessor.sparse_count == 0u || accessor.sparse_count > accessor.count ||
                !indices || !indices->is(Type::Object) || !values || !values->is(Type::Object))
                return fail(error, "invalid glTF sparse accessor");
            accessor.sparse_indices_view = GltfJson::integer(indices->get("bufferView"));
            accessor.sparse_indices_offset = GltfJson::sizeValue(indices->get("byteOffset"));
            accessor.sparse_indices_component = GltfJson::integer(indices->get("componentType"), 0);
            accessor.sparse_values_view = GltfJson::integer(values->get("bufferView"));
            accessor.sparse_values_offset = GltfJson::sizeValue(values->get("byteOffset"));
            if ((accessor.sparse_indices_component != 5121 && accessor.sparse_indices_component != 5123 &&
                 accessor.sparse_indices_component != 5125) ||
                accessor.sparse_indices_view < 0 || accessor.sparse_values_view < 0 ||
                static_cast<std::size_t>(accessor.sparse_indices_view) >= context->views.size() ||
                static_cast<std::size_t>(accessor.sparse_values_view) >= context->views.size())
                return fail(error, "invalid glTF sparse accessor buffers");
        }
        context->accessors.push_back(std::move(accessor));
    }
    return true;
}

bool decodeFloats(
    const Context& context,
    int accessor_index,
    std::vector<float> *out,
    std::size_t expected_components,
    std::string *error)
{
    if (!out || accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= context.accessors.size())
        return fail(error, "glTF references invalid accessor");
    const Accessor& accessor = context.accessors[static_cast<std::size_t>(accessor_index)];
    const Layout value_layout = layout(accessor.type, accessor.component_type);
    if (value_layout.element_size == 0u) return fail(error, "unsupported glTF accessor layout");
    if (expected_components != 0u && value_layout.components != expected_components)
        return fail(error, "glTF accessor has unexpected component count");
    if (accessor.count > std::numeric_limits<std::size_t>::max() / value_layout.components)
        return fail(error, "glTF accessor output size overflows");
    out->assign(accessor.count * value_layout.components, 0.0f);

    if (accessor.view >= 0) {
        const BufferView& view = context.views[static_cast<std::size_t>(accessor.view)];
        const std::size_t stride = view.stride == 0u ? value_layout.element_size : view.stride;
        if (stride < value_layout.element_size) return fail(error, "glTF accessor stride is smaller than element size");
        std::size_t bytes = 0u;
        if (!requiredBytes(accessor.count, stride, value_layout.element_size, &bytes))
            return fail(error, "glTF accessor byte range overflows");
        const std::uint8_t *base = nullptr;
        if (!range(context, accessor.view, accessor.offset, bytes, &base, error)) return false;
        for (std::size_t element = 0u; element < accessor.count; ++element) {
            for (std::size_t component = 0u; component < value_layout.components; ++component) {
                (*out)[element * value_layout.components + component] = asFloat(
                    base + element * stride + componentOffset(value_layout, component),
                    accessor.component_type,
                    accessor.normalized
                );
            }
        }
    }

    if (accessor.sparse_count != 0u) {
        const std::uint8_t *indices = nullptr;
        const std::uint8_t *values = nullptr;
        std::size_t index_size = 0u;
        if (!sparseRanges(context, accessor, value_layout, &indices, &index_size, &values, error)) return false;
        for (std::size_t sparse = 0u; sparse < accessor.sparse_count; ++sparse) {
            std::uint32_t destination = 0u;
            if (!asUnsigned(indices + sparse * index_size, accessor.sparse_indices_component, &destination) ||
                destination >= accessor.count)
                return fail(error, "invalid glTF sparse accessor index");
            for (std::size_t component = 0u; component < value_layout.components; ++component) {
                (*out)[static_cast<std::size_t>(destination) * value_layout.components + component] = asFloat(
                    values + sparse * value_layout.element_size + componentOffset(value_layout, component),
                    accessor.component_type,
                    accessor.normalized
                );
            }
        }
    }
    return true;
}

bool decodeUnsigned(
    const Context& context,
    int accessor_index,
    std::vector<std::uint32_t> *out,
    std::size_t expected_components,
    std::string *error)
{
    if (!out || accessor_index < 0 || static_cast<std::size_t>(accessor_index) >= context.accessors.size())
        return fail(error, "glTF references invalid unsigned accessor");
    const Accessor& accessor = context.accessors[static_cast<std::size_t>(accessor_index)];
    if (accessor.component_type != 5121 && accessor.component_type != 5123 && accessor.component_type != 5125)
        return fail(error, "glTF unsigned accessor uses signed or floating component type");
    const Layout value_layout = layout(accessor.type, accessor.component_type);
    if (value_layout.element_size == 0u) return fail(error, "unsupported glTF unsigned accessor layout");
    if (expected_components != 0u && value_layout.components != expected_components)
        return fail(error, "glTF unsigned accessor has unexpected component count");
    if (accessor.count > std::numeric_limits<std::size_t>::max() / value_layout.components)
        return fail(error, "glTF unsigned accessor output size overflows");
    out->assign(accessor.count * value_layout.components, 0u);

    if (accessor.view >= 0) {
        const BufferView& view = context.views[static_cast<std::size_t>(accessor.view)];
        const std::size_t stride = view.stride == 0u ? value_layout.element_size : view.stride;
        if (stride < value_layout.element_size) return fail(error, "glTF unsigned accessor stride is too small");
        std::size_t bytes = 0u;
        if (!requiredBytes(accessor.count, stride, value_layout.element_size, &bytes))
            return fail(error, "glTF unsigned accessor byte range overflows");
        const std::uint8_t *base = nullptr;
        if (!range(context, accessor.view, accessor.offset, bytes, &base, error)) return false;
        for (std::size_t element = 0u; element < accessor.count; ++element) {
            for (std::size_t component = 0u; component < value_layout.components; ++component) {
                if (!asUnsigned(
                    base + element * stride + componentOffset(value_layout, component),
                    accessor.component_type,
                    &(*out)[element * value_layout.components + component]
                )) return fail(error, "failed to decode glTF unsigned component");
            }
        }
    }

    if (accessor.sparse_count != 0u) {
        const std::uint8_t *indices = nullptr;
        const std::uint8_t *values = nullptr;
        std::size_t index_size = 0u;
        if (!sparseRanges(context, accessor, value_layout, &indices, &index_size, &values, error)) return false;
        for (std::size_t sparse = 0u; sparse < accessor.sparse_count; ++sparse) {
            std::uint32_t destination = 0u;
            if (!asUnsigned(indices + sparse * index_size, accessor.sparse_indices_component, &destination) ||
                destination >= accessor.count)
                return fail(error, "invalid glTF sparse unsigned accessor index");
            for (std::size_t component = 0u; component < value_layout.components; ++component) {
                if (!asUnsigned(
                    values + sparse * value_layout.element_size + componentOffset(value_layout, component),
                    accessor.component_type,
                    &(*out)[static_cast<std::size_t>(destination) * value_layout.components + component]
                )) return fail(error, "failed to decode glTF sparse unsigned component");
            }
        }
    }
    return true;
}

} // namespace Models::Formats::GltfData
