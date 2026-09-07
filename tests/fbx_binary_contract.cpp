#include "Models/Formats/FbxBinary.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void appendU32(Bytes& out, std::uint32_t value)
{
    for (unsigned shift = 0u; shift < 32u; shift += 8u) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void appendU64(Bytes& out, std::uint64_t value)
{
    for (unsigned shift = 0u; shift < 64u; shift += 8u) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void patchU32(Bytes& out, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i) out[offset + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}
void patchU64(Bytes& out, std::size_t offset, std::uint64_t value)
{
    for (unsigned i = 0u; i < 8u; ++i) out[offset + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

Bytes header(std::uint32_t version)
{
    static constexpr std::uint8_t magic[23] = {
        'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',' ',' ',0,0x1a,0
    };
    Bytes out(magic, magic + 23u);
    appendU32(out, version);
    return out;
}

void appendString7400(Bytes& out, const std::string& name, const std::string& value)
{
    const std::size_t start = out.size();
    appendU32(out,0u); appendU32(out,1u); appendU32(out,static_cast<std::uint32_t>(5u + value.size()));
    out.push_back(static_cast<std::uint8_t>(name.size())); out.insert(out.end(),name.begin(),name.end());
    out.push_back('S'); appendU32(out,static_cast<std::uint32_t>(value.size())); out.insert(out.end(),value.begin(),value.end());
    patchU32(out,start,static_cast<std::uint32_t>(out.size()));
}

void appendCompressedDoubles7400(Bytes& out)
{
    static constexpr std::uint8_t compressed[] = {
        0x78,0x9c,0x63,0x60,0x00,0x81,0x2f,0xf6,0x60,0x8a,0x81,0xe5,0x00,0x84,0xe6,0x73,
        0x80,0xd0,0x02,0x50,0x5a,0x04,0x4a,0x4b,0x38,0x00,0x00,0x5c,0x1b,0x03,0x42
    };
    const std::string name = "Vertices";
    const std::size_t start = out.size();
    appendU32(out,0u); appendU32(out,1u); appendU32(out,1u + 12u + sizeof(compressed));
    out.push_back(static_cast<std::uint8_t>(name.size())); out.insert(out.end(),name.begin(),name.end());
    out.push_back('d'); appendU32(out,6u); appendU32(out,1u); appendU32(out,sizeof(compressed));
    out.insert(out.end(),compressed,compressed + sizeof(compressed));
    patchU32(out,start,static_cast<std::uint32_t>(out.size()));
}

Bytes make7400()
{
    Bytes out = header(7400u);
    appendCompressedDoubles7400(out);
    appendString7400(out,"Creator","native-binary-reader");
    out.insert(out.end(),13u,0u);
    return out;
}

Bytes make7500()
{
    Bytes out = header(7500u);
    const std::string name = "VersionProbe";
    const std::size_t start = out.size();
    appendU64(out,0u); appendU64(out,1u); appendU64(out,5u);
    out.push_back(static_cast<std::uint8_t>(name.size())); out.insert(out.end(),name.begin(),name.end());
    out.push_back('I'); appendU32(out,7500u);
    patchU64(out,start,out.size());
    out.insert(out.end(),25u,0u);
    return out;
}

} // namespace

int main()
{
    Models::FbxDocument::RawDocument document;
    std::string error;

    const Bytes f7400 = make7400();
    assert(Models::FbxBinary::matches(f7400.data(), f7400.size()));
    assert(Models::FbxBinary::parse(f7400.data(), f7400.size(), &document, &error));
    assert(document.binary && document.version == 7400u);
    const auto *vertices = document.root.child("Vertices");
    assert(vertices);
    const auto values = vertices->numericArray();
    assert(values.size() == 6u);
    assert(std::abs(values[0] - 1.25) < 1.0e-12);
    assert(std::abs(values[1] + 2.5) < 1.0e-12);
    assert(std::abs(values[5] - 6.0) < 1.0e-12);
    assert(document.root.child("Creator")->properties[0].asString() == "native-binary-reader");

    const Bytes f7500 = make7500();
    assert(Models::FbxBinary::parse(f7500.data(), f7500.size(), &document, &error));
    assert(document.version == 7500u);
    assert(document.root.child("VersionProbe")->properties[0].asInt64() == 7500);

    Bytes malformed = f7400;
    malformed[27] = 0xffu; malformed[28] = 0xffu; malformed[29] = 0xffu; malformed[30] = 0x7fu;
    assert(!Models::FbxBinary::parse(malformed.data(), malformed.size(), &document, &error));
    assert(!error.empty());
    return 0;
}
