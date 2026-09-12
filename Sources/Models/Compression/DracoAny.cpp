#include "Models/Compression/Draco.hpp"
#include "Models/Compression/DracoEdgeBreakerComplete.hpp"
#include "Models/Compression/DracoEdgeBreakerPredictive.hpp"
#include "Models/Compression/DracoEdgeBreakerValence.hpp"
#include "Models/Compression/DracoSequentialComplete.hpp"

#include <cstring>

namespace Models::Compression {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

class HeaderReader {
public:
    HeaderReader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    bool u8(std::uint8_t *out)
    {
        if (!out || position_ >= size_) return false;
        *out = data_[position_++];
        return true;
    }

    bool skip(std::size_t count)
    {
        if (count > size_ - position_) return false;
        position_ += count;
        return true;
    }

    bool var32(std::uint32_t *out)
    {
        if (!out) return false;
        std::uint32_t value = 0u;
        unsigned int shift = 0u;
        for (unsigned int index = 0u; index < 5u; ++index) {
            std::uint8_t byte = 0u;
            if (!u8(&byte)) return false;
            if (shift == 28u && (byte & 0xf0u) != 0u) return false;
            value |= static_cast<std::uint32_t>(byte & 0x7fu) << shift;
            if ((byte & 0x80u) == 0u) {
                *out = value;
                return true;
            }
            shift += 7u;
        }
        return false;
    }

    std::size_t position() const { return position_; }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t size_ = 0u;
    std::size_t position_ = 0u;
};

bool skipMetadataElement(HeaderReader& reader)
{
    std::uint32_t entries = 0u;
    if (!reader.var32(&entries)) return false;
    for (std::uint32_t index = 0u; index < entries; ++index) {
        std::uint8_t key_size = 0u;
        std::uint8_t value_size = 0u;
        if (!reader.u8(&key_size) || !reader.skip(key_size) ||
            !reader.u8(&value_size) || !reader.skip(value_size))
            return false;
    }
    std::uint32_t children = 0u;
    if (!reader.var32(&children)) return false;
    for (std::uint32_t index = 0u; index < children; ++index) {
        std::uint8_t key_size = 0u;
        if (!reader.u8(&key_size) || !reader.skip(key_size) || !skipMetadataElement(reader)) return false;
    }
    return true;
}

bool edgeBreakerTraversalType(
    const std::uint8_t *data,
    std::size_t size,
    std::uint8_t *traversal,
    std::string *error)
{
    if (!data || !traversal || size < 12u) return fail(error, "truncated Draco EdgeBreaker stream");
    HeaderReader reader(data + 11u, size - 11u);
    const std::uint16_t flags = static_cast<std::uint16_t>(data[9]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[10]) << 8u);
    if ((flags & 0x8000u) != 0u) {
        std::uint32_t attributes = 0u;
        if (!reader.var32(&attributes)) return fail(error, "truncated Draco metadata count");
        for (std::uint32_t index = 0u; index < attributes; ++index) {
            std::uint32_t id = 0u;
            if (!reader.var32(&id) || !skipMetadataElement(reader))
                return fail(error, "truncated Draco metadata");
            (void)id;
        }
        if (!skipMetadataElement(reader)) return fail(error, "truncated Draco metadata");
    }
    if (!reader.u8(traversal)) return fail(error, "truncated Draco EdgeBreaker traversal type");
    (void)reader.position();
    return true;
}

} // namespace

bool decodeDracoAny(
    const std::uint8_t *data,
    std::size_t size,
    DracoMesh *mesh,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !mesh || size < 11u) return fail(error, "truncated Draco stream");
    if (std::memcmp(data, "DRACO", 5u) != 0) return fail(error, "invalid Draco magic");
    if (data[5] != 2u || data[6] > 2u) return fail(error, "unsupported Draco bitstream version");
    if (data[7] != 1u) return fail(error, "Draco stream is not a triangular mesh");

    switch (data[8]) {
        case 0u:
            return decodeDracoSequentialComplete(data, size, mesh, error);
        case 1u: {
            std::uint8_t traversal = 0u;
            if (!edgeBreakerTraversalType(data, size, &traversal, error)) return false;
            switch (traversal) {
                case 0u:
                    return decodeDracoEdgeBreakerComplete(data, size, mesh, error);
                case 1u:
                    return decodeDracoEdgeBreakerPredictive(data, size, mesh, error);
                case 2u:
                    return decodeDracoEdgeBreakerValence(data, size, mesh, error);
                default:
                    return fail(error, "unknown Draco EdgeBreaker traversal encoding");
            }
        }
        default:
            return fail(error, "unsupported Draco mesh encoding method");
    }
}

} // namespace Models::Compression
