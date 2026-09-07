#include "Models/Formats/FbxBinary.hpp"

#include "Models/Compression/Deflate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Models::FbxBinary {
namespace {

constexpr std::array<std::uint8_t, 23> kMagic = {
    'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',' ',' ',0,0x1a,0
};
constexpr std::size_t kMaximumDepth = 256u;
constexpr std::size_t kMaximumNodes = 1000000u;
constexpr std::size_t kMaximumProperties = 10000000u;

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

struct Reader {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0u;
    std::size_t pos = 0u;
    std::string *error = nullptr;

    bool need(std::size_t bytes)
    {
        if (pos > size || bytes > size - pos) return fail(error, "unexpected end of FBX binary data");
        return true;
    }

    template <typename T>
    bool read(T *out)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!out || !need(sizeof(T))) return false;
        std::memcpy(out, data + pos, sizeof(T));
        pos += sizeof(T);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        std::reverse(
            reinterpret_cast<std::uint8_t *>(out),
            reinterpret_cast<std::uint8_t *>(out) + sizeof(T)
        );
#endif
        return true;
    }

    bool readBytes(std::size_t bytes, const std::uint8_t **out)
    {
        if (!out || !need(bytes)) return false;
        *out = data + pos;
        pos += bytes;
        return true;
    }

    bool skip(std::size_t bytes)
    {
        if (!need(bytes)) return false;
        pos += bytes;
        return true;
    }
};

bool zeroRecord(const Reader& reader, std::size_t bytes)
{
    if (reader.pos > reader.size || bytes > reader.size - reader.pos) return false;
    for (std::size_t i = 0u; i < bytes; ++i) {
        if (reader.data[reader.pos + i] != 0u) return false;
    }
    return true;
}

template <typename T>
bool decodeArray(
    Reader *reader,
    std::uint32_t count,
    std::uint32_t encoding,
    std::uint32_t stored_bytes,
    std::vector<T> *out)
{
    if (!reader || !out) return fail(reader ? reader->error : nullptr, "invalid FBX array state");
    if (count != 0u && sizeof(T) > std::numeric_limits<std::size_t>::max() / count) {
        return fail(reader->error, "FBX array size overflow");
    }
    const std::size_t expected = static_cast<std::size_t>(count) * sizeof(T);
    const std::uint8_t *payload = nullptr;
    if (!reader->readBytes(stored_bytes, &payload)) return false;

    std::vector<std::uint8_t> raw;
    if (encoding == 0u) {
        if (stored_bytes != expected) return fail(reader->error, "uncompressed FBX array byte size mismatch");
        raw.assign(payload, payload + stored_bytes);
    } else if (encoding == 1u) {
        Models::Compression::InflateOptions options;
        options.max_output = expected;
        if (!Models::Compression::inflateZlib(payload, stored_bytes, &raw, reader->error, options)) return false;
        if (raw.size() != expected) return fail(reader->error, "inflated FBX array byte size mismatch");
    } else {
        return fail(reader->error, "unsupported FBX array encoding");
    }

    out->resize(count);
    if (expected != 0u) std::memcpy(out->data(), raw.data(), expected);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (T& value : *out) {
        std::reverse(
            reinterpret_cast<std::uint8_t *>(&value),
            reinterpret_cast<std::uint8_t *>(&value) + sizeof(T)
        );
    }
#endif
    return true;
}

bool readProperty(Reader *reader, FbxDocument::Property *out)
{
    if (!reader || !out) return fail(reader ? reader->error : nullptr, "invalid FBX property output");
    std::uint8_t code = 0u;
    if (!reader->read(&code)) return false;
    out->type = static_cast<char>(code);

    switch (out->type) {
        case 'Y': {
            std::int16_t value = 0;
            if (!reader->read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'C': {
            std::uint8_t value = 0u;
            if (!reader->read(&value)) return false;
            out->value = (value == 'T') || (value != 'F' && (value & 1u) != 0u);
            return true;
        }
        case 'I': {
            std::int32_t value = 0;
            if (!reader->read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'F': {
            float value = 0.0f;
            if (!reader->read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'D': {
            double value = 0.0;
            if (!reader->read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'L': {
            std::int64_t value = 0;
            if (!reader->read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'S':
        case 'R': {
            std::uint32_t length = 0u;
            if (!reader->read(&length)) return false;
            const std::uint8_t *bytes = nullptr;
            if (!reader->readBytes(length, &bytes)) return false;
            if (out->type == 'S') {
                out->value = std::string(reinterpret_cast<const char *>(bytes), length);
            } else {
                out->value = FbxDocument::Bytes(bytes, bytes + length);
            }
            return true;
        }
        case 'f':
        case 'd':
        case 'i':
        case 'l':
        case 'b': {
            std::uint32_t count = 0u;
            std::uint32_t encoding = 0u;
            std::uint32_t stored = 0u;
            if (!reader->read(&count) || !reader->read(&encoding) || !reader->read(&stored)) return false;
            if (out->type == 'f') {
                std::vector<float> values;
                if (!decodeArray(reader, count, encoding, stored, &values)) return false;
                out->value = std::move(values);
            } else if (out->type == 'd') {
                std::vector<double> values;
                if (!decodeArray(reader, count, encoding, stored, &values)) return false;
                out->value = std::move(values);
            } else if (out->type == 'i') {
                std::vector<std::int32_t> values;
                if (!decodeArray(reader, count, encoding, stored, &values)) return false;
                out->value = std::move(values);
            } else if (out->type == 'l') {
                std::vector<std::int64_t> values;
                if (!decodeArray(reader, count, encoding, stored, &values)) return false;
                out->value = std::move(values);
            } else {
                std::vector<std::uint8_t> values;
                if (!decodeArray(reader, count, encoding, stored, &values)) return false;
                out->value = FbxDocument::Bytes(values.begin(), values.end());
            }
            return true;
        }
        default:
            return fail(reader->error, std::string("unsupported FBX property type: ") + out->type);
    }
}

bool readNode(
    Reader *reader,
    std::uint32_t version,
    std::size_t depth,
    std::size_t *node_count,
    FbxDocument::Node *out,
    bool *was_null)
{
    if (!reader || !node_count || !out || !was_null) return fail(reader ? reader->error : nullptr, "invalid FBX node state");
    if (depth > kMaximumDepth) return fail(reader->error, "FBX node nesting exceeds limit");
    if (*node_count >= kMaximumNodes) return fail(reader->error, "FBX node count exceeds limit");

    *was_null = false;
    const bool wide = version >= 7500u;
    const std::size_t sentinel = wide ? 25u : 13u;
    if (zeroRecord(*reader, sentinel)) {
        reader->pos += sentinel;
        *was_null = true;
        return true;
    }

    std::uint64_t end_offset = 0u;
    std::uint64_t property_count = 0u;
    std::uint64_t property_bytes = 0u;
    if (wide) {
        if (!reader->read(&end_offset) || !reader->read(&property_count) || !reader->read(&property_bytes)) return false;
    } else {
        std::uint32_t end32 = 0u;
        std::uint32_t count32 = 0u;
        std::uint32_t bytes32 = 0u;
        if (!reader->read(&end32) || !reader->read(&count32) || !reader->read(&bytes32)) return false;
        end_offset = end32;
        property_count = count32;
        property_bytes = bytes32;
    }

    std::uint8_t name_length = 0u;
    if (!reader->read(&name_length)) return false;
    if (end_offset == 0u) {
        *was_null = true;
        return true;
    }
    if (end_offset > reader->size || end_offset < reader->pos) return fail(reader->error, "invalid FBX node end offset");
    const std::uint8_t *name = nullptr;
    if (!reader->readBytes(name_length, &name)) return false;
    out->name.assign(reinterpret_cast<const char *>(name), name_length);

    if (property_count > kMaximumProperties) return fail(reader->error, "FBX property count exceeds limit");
    if (property_bytes > end_offset - reader->pos) return fail(reader->error, "FBX property region exceeds node bounds");
    const std::size_t property_start = reader->pos;
    out->properties.reserve(static_cast<std::size_t>(property_count));
    for (std::uint64_t i = 0u; i < property_count; ++i) {
        FbxDocument::Property property;
        if (!readProperty(reader, &property)) return false;
        if (reader->pos > end_offset) return fail(reader->error, "FBX property exceeds node bounds");
        out->properties.push_back(std::move(property));
    }
    const std::size_t consumed = reader->pos - property_start;
    if (consumed > property_bytes) return fail(reader->error, "FBX property list length mismatch");
    if (consumed < property_bytes && !reader->skip(static_cast<std::size_t>(property_bytes) - consumed)) return false;

    ++*node_count;
    while (reader->pos < end_offset) {
        if (zeroRecord(*reader, sentinel)) {
            reader->pos += sentinel;
            break;
        }
        FbxDocument::Node child;
        bool null_child = false;
        if (!readNode(reader, version, depth + 1u, node_count, &child, &null_child)) return false;
        if (null_child) break;
        out->children.push_back(std::move(child));
    }
    if (reader->pos > end_offset) return fail(reader->error, "FBX node exceeded declared end offset");
    if (reader->pos < end_offset) reader->pos = static_cast<std::size_t>(end_offset);
    return true;
}

} // namespace

bool matches(const std::uint8_t *data, std::size_t size)
{
    return data && size >= kMagic.size() && std::memcmp(data, kMagic.data(), kMagic.size()) == 0;
}

bool parse(
    const std::uint8_t *data,
    std::size_t size,
    FbxDocument::RawDocument *out,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !out) return fail(error, "invalid binary FBX input");
    *out = {};
    if (!matches(data, size)) return fail(error, "invalid FBX binary magic");
    if (size < 27u) return fail(error, "FBX binary header is truncated");

    Reader reader{data, size, 23u, error};
    std::uint32_t version = 0u;
    if (!reader.read(&version)) return false;
    out->version = version;
    out->binary = true;

    const std::size_t sentinel = version >= 7500u ? 25u : 13u;
    std::size_t node_count = 0u;
    while (reader.pos + sentinel <= size) {
        if (zeroRecord(reader, sentinel)) break;
        FbxDocument::Node node;
        bool was_null = false;
        if (!readNode(&reader, version, 0u, &node_count, &node, &was_null)) return false;
        if (was_null) break;
        out->root.children.push_back(std::move(node));
    }
    return true;
}

} // namespace Models::FbxBinary
