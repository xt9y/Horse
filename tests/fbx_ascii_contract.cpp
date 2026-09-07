#include "Models/Formats/FbxAscii.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>

int main()
{
    const char *fixture = R"FBX(
; comment
FBXHeaderExtension:  {
    FBXVersion: 7400
}
Geometry: 100, "Geometry::Triangle", "Mesh" {
    Vertices: *9 { a: 0,0,0, 1,0,0, 0,1e0,0 }
    PolygonVertexIndex: *3 { a: 0,1,-3 }
    Note: "escaped \"quote\""
}
)FBX";

    Models::FbxDocument::RawDocument document;
    std::string error;
    assert(Models::FbxAscii::parse(
        reinterpret_cast<const std::uint8_t *>(fixture),
        std::char_traits<char>::length(fixture),
        &document,
        &error
    ));
    assert(error.empty());
    assert(!document.binary && document.version == 7400u);
    const auto *geometry = document.root.child("Geometry");
    assert(geometry && geometry->properties.size() == 3u);
    assert(geometry->properties[0].asInt64() == 100);
    const auto *vertices = geometry->child("Vertices");
    assert(vertices);
    const auto values = vertices->numericArray();
    assert(values.size() == 9u);
    assert(std::abs(values[7] - 1.0) < 1.0e-12);
    assert(geometry->child("PolygonVertexIndex")->numericArray().size() == 3u);
    assert(geometry->child("Note")->properties[0].asString() == "escaped \"quote\"");

    const char *bad_count = "Vertices: *2 { a: 1 }\n";
    assert(!Models::FbxAscii::parse(
        reinterpret_cast<const std::uint8_t *>(bad_count),
        std::char_traits<char>::length(bad_count),
        &document,
        &error
    ));
    assert(error.find("count") != std::string::npos);

    const char *bad_string = "Node: \"unterminated\n";
    assert(!Models::FbxAscii::parse(
        reinterpret_cast<const std::uint8_t *>(bad_string),
        std::char_traits<char>::length(bad_string),
        &document,
        &error
    ));
    assert(!error.empty());

    const char *bad_brace = "Node: {\nChild: 1\n";
    assert(!Models::FbxAscii::parse(
        reinterpret_cast<const std::uint8_t *>(bad_brace),
        std::char_traits<char>::length(bad_brace),
        &document,
        &error
    ));
    assert(error.find("unterminated") != std::string::npos);
    return 0;
}
