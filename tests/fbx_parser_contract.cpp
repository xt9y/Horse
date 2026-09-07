#include "Models/Formats/FbxParser.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

using Bytes = std::vector<std::uint8_t>;

void appendU32(Bytes& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

void patchU32(Bytes& out, std::size_t offset, std::uint32_t value)
{
    assert(offset + 4u <= out.size());
    out[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    out[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    out[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    out[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

Bytes compressedDoubles(const std::vector<double>& values)
{
    const auto *source = reinterpret_cast<const Bytef *>(values.data());
    const uLong source_size = static_cast<uLong>(values.size() * sizeof(double));
    uLongf compressed_size = compressBound(source_size);
    Bytes compressed(static_cast<std::size_t>(compressed_size));
    const int status = compress2(
        reinterpret_cast<Bytef *>(compressed.data()),
        &compressed_size,
        source,
        source_size,
        Z_BEST_SPEED
    );
    assert(status == Z_OK);
    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

void appendArrayNode(Bytes& out, const std::string& name, const std::vector<double>& values)
{
    const Bytes compressed = compressedDoubles(values);
    const std::size_t node_start = out.size();
    appendU32(out, 0u); // end offset, patched below
    appendU32(out, 1u); // property count
    const std::uint32_t property_bytes = static_cast<std::uint32_t>(
        1u + 4u + 4u + 4u + compressed.size()
    );
    appendU32(out, property_bytes);
    out.push_back(static_cast<std::uint8_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());

    out.push_back(static_cast<std::uint8_t>('d'));
    appendU32(out, static_cast<std::uint32_t>(values.size()));
    appendU32(out, 1u); // zlib encoded
    appendU32(out, static_cast<std::uint32_t>(compressed.size()));
    out.insert(out.end(), compressed.begin(), compressed.end());

    patchU32(out, node_start, static_cast<std::uint32_t>(out.size()));
}

void appendStringNode(Bytes& out, const std::string& name, const std::string& value)
{
    const std::size_t node_start = out.size();
    appendU32(out, 0u);
    appendU32(out, 1u);
    appendU32(out, static_cast<std::uint32_t>(1u + 4u + value.size()));
    out.push_back(static_cast<std::uint8_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(static_cast<std::uint8_t>('S'));
    appendU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    patchU32(out, node_start, static_cast<std::uint32_t>(out.size()));
}

Bytes makeBinary7400()
{
    static constexpr std::uint8_t magic[23] = {
        'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',' ',' ',0,0x1a,0
    };
    Bytes out(magic, magic + 23u);
    appendU32(out, 7400u);
    appendArrayNode(out, "Vertices", {1.25, -2.5, 3.75, 4.0, 5.0, 6.0});
    appendStringNode(out, "Creator", "native-parser-contract");
    out.insert(out.end(), 13u, 0u);
    return out;
}

} // namespace

int main()
{
    const Bytes bytes = makeBinary7400();
    Models::FbxParser::Document document;
    std::string error;
    assert(Models::FbxParser::parseMemory(bytes.data(), bytes.size(), &document, &error));
    assert(error.empty());
    assert(document.binary);
    assert(document.version == 7400u);

    const Models::FbxParser::Node *vertices = document.root.child("Vertices");
    assert(vertices);
    const std::vector<double> values = vertices->numericArray();
    assert(values.size() == 6u);
    assert(std::abs(values[0] - 1.25) < 1.0e-12);
    assert(std::abs(values[1] + 2.5) < 1.0e-12);
    assert(std::abs(values[5] - 6.0) < 1.0e-12);

    const Models::FbxParser::Node *creator = document.root.child("Creator");
    assert(creator && creator->properties.size() == 1u);
    assert(creator->properties[0].asString() == "native-parser-contract");
    return 0;
}
