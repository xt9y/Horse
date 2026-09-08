#include "fbx_fixture.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

namespace {
using namespace HorseFbxTest;

void testConcaveNgon()
{
    const auto document = load(writeFixture(
        "horse-fbx-ngon",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Concave\", \"Mesh\" {\n"
        "    Vertices: *15 { a: 0,0,0,2,0,0,2,2,0,1,1,0,0,2,0 }\n"
        "    PolygonVertexIndex: *5 { a: 0,1,2,3,-5 }\n"
        "  }\n"
        "  Model: 2, \"Model::Concave\", \"Mesh\" {}\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "}\n"));
    assert(document.parts.size() == 1u);
    assert(document.parts[0].mesh.indices.size() == 9u);
}

void testMirroredNonUniformNormal()
{
    const auto document = load(writeFixture(
        "horse-fbx-mirrored-normal",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Mirror\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "    LayerElementNormal: 0 {\n"
        "      MappingInformationType: \"ByPolygonVertex\"\n"
        "      ReferenceInformationType: \"Direct\"\n"
        "      Normals: *9 { a: 1,1,0,1,1,0,1,1,0 }\n"
        "    }\n"
        "  }\n"
        "  Model: 2, \"Model::Mirror\", \"Mesh\" {\n"
        "    Properties70: {\n"
        "      P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",-2,1,0.5\n"
        "    }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "}\n"));
    const auto& mesh = document.parts[0].mesh;
    const float expected_x = -1.0f / std::sqrt(5.0f);
    const float expected_y = 2.0f / std::sqrt(5.0f);
    for (const auto& vertex : mesh.vertices) {
        assert(near(vertex.normal.x, expected_x));
        assert(near(vertex.normal.y, expected_y));
        assert(near(vertex.normal.z, 0.0f));
    }
    const auto& a = mesh.vertices[mesh.indices[0]].position;
    const auto& b = mesh.vertices[mesh.indices[1]].position;
    const auto& c = mesh.vertices[mesh.indices[2]].position;
    assert((b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x) > 0.0f);
}

void testPerPolygonMaterials()
{
    const auto document = load(writeFixture(
        "horse-fbx-material-slots",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::TwoParts\", \"Mesh\" {\n"
        "    Vertices: *12 { a: 0,0,0,1,0,0,1,1,0,0,1,0 }\n"
        "    PolygonVertexIndex: *6 { a: 0,1,-3,0,2,-4 }\n"
        "    LayerElementMaterial: 0 {\n"
        "      MappingInformationType: \"ByPolygon\"\n"
        "      ReferenceInformationType: \"IndexToDirect\"\n"
        "      Materials: *2 { a: 0,1 }\n"
        "    }\n"
        "  }\n"
        "  Model: 2, \"Model::TwoParts\", \"Mesh\" {}\n"
        "  Material: 10, \"Material::Red\", \"\" {\n"
        "    Properties70: { P: \"DiffuseColor\", \"Color\", \"\", \"A\",1,0,0 }\n"
        "  }\n"
        "  Material: 11, \"Material::Green\", \"\" {\n"
        "    Properties70: { P: \"DiffuseColor\", \"Color\", \"\", \"A\",0,1,0 }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "  C: \"OO\",10,2\n"
        "  C: \"OO\",11,2\n"
        "}\n"));
    assert(document.parts.size() == 2u);
    assert(document.parts[0].material.name == "Red");
    assert(document.parts[1].material.name == "Green");
}

void testInvalidLayerFails()
{
    const std::string path = writeFixture(
        "horse-fbx-invalid-layer",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::BadUv\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "    LayerElementUV: 0 {\n"
        "      MappingInformationType: \"ByPolygonVertex\"\n"
        "      ReferenceInformationType: \"IndexToDirect\"\n"
        "      UV: *4 { a: 0,0,1,1 }\n"
        "      UVIndex: *3 { a: 0,9,0 }\n"
        "    }\n"
        "  }\n"
        "  Model: 2, \"Model::BadUv\", \"Mesh\" {}\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "}\n");
    Models::Fbx::Document document;
    std::string error;
    assert(!Models::Fbx::load(path, &document, &error));
    assert(error.find("UV") != std::string::npos || error.find("uv") != std::string::npos);
}

float maxX(const Models::MeshData& mesh)
{
    float value = -1.0e30f;
    for (const auto& vertex : mesh.vertices) value = std::max(value, vertex.position.x);
    return value;
}

void testInheritanceModes()
{
    for (int mode = 0; mode <= 2; ++mode) {
        const auto document = load(writeFixture(
            "horse-fbx-inherit-" + std::to_string(mode),
            "Objects: {\n"
            "  Geometry: 1, \"Geometry::Child\", \"Mesh\" {\n"
            "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
            "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
            "  }\n"
            "  Model: 2, \"Model::Parent\", \"Null\" {\n"
            "    Properties70: {\n"
            "      P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",2,2,2\n"
            "    }\n"
            "  }\n"
            "  Model: 3, \"Model::Child\", \"Mesh\" {\n"
            "    Properties70: {\n"
            "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0\n"
            "      P: \"InheritType\", \"enum\", \"\", \"\"," + std::to_string(mode) + "\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Connections: {\n"
            "  C: \"OO\",1,3\n"
            "  C: \"OO\",3,2\n"
            "}\n"));
        const float expected = mode == 2 ? 3.0f : 4.0f;
        assert(near(maxX(document.parts[0].mesh), expected));
    }
}

void testGeometricTransformDoesNotInherit()
{
    const auto document = load(writeFixture(
        "horse-fbx-geometric-parent",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Child\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::Parent\", \"Null\" {\n"
        "    Properties70: {\n"
        "      P: \"GeometricTranslation\", \"Vector3D\", \"Vector\", \"\",10,0,0\n"
        "    }\n"
        "  }\n"
        "  Model: 3, \"Model::Child\", \"Mesh\" {\n"
        "    Properties70: {\n"
        "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0\n"
        "    }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,3\n"
        "  C: \"OO\",3,2\n"
        "}\n"));
    assert(near(maxX(document.parts[0].mesh), 2.0f));
}

} // namespace

int main()
{
    testConcaveNgon();
    testMirroredNonUniformNormal();
    testPerPolygonMaterials();
    testInvalidLayerFails();
    testInheritanceModes();
    testGeometricTransformDoesNotInherit();
    return 0;
}
