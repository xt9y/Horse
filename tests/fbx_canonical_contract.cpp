#include "Models/Formats/Fbx.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <string>

namespace {

bool near(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

std::string writeFixture(const char *name, const std::string& body)
{
    const std::string path = std::string("/tmp/") + name + ".fbx";
    std::ofstream file(path, std::ios::binary);
    file << "; FBX 7.4.0 project file\n" << body;
    file.close();
    return path;
}

std::string globalSettings(
    double unit_scale,
    int up_axis,
    int up_sign,
    int front_axis,
    int front_sign,
    int coord_axis,
    int coord_sign)
{
    return
        "GlobalSettings: {\n"
        "  Properties70: {\n"
        "    P: \"UnitScaleFactor\", \"double\", \"Number\", \"\"," + std::to_string(unit_scale) + "\n"
        "    P: \"UpAxis\", \"int\", \"Integer\", \"\"," + std::to_string(up_axis) + "\n"
        "    P: \"UpAxisSign\", \"int\", \"Integer\", \"\"," + std::to_string(up_sign) + "\n"
        "    P: \"FrontAxis\", \"int\", \"Integer\", \"\"," + std::to_string(front_axis) + "\n"
        "    P: \"FrontAxisSign\", \"int\", \"Integer\", \"\"," + std::to_string(front_sign) + "\n"
        "    P: \"CoordAxis\", \"int\", \"Integer\", \"\"," + std::to_string(coord_axis) + "\n"
        "    P: \"CoordAxisSign\", \"int\", \"Integer\", \"\"," + std::to_string(coord_sign) + "\n"
        "  }\n"
        "}\n";
}

std::string triangleGeometry()
{
    return
        "  Geometry: 1, \"Geometry::Tri\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,0,1 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "    LayerElementNormal: 0 {\n"
        "      MappingInformationType: \"ByPolygonVertex\"\n"
        "      ReferenceInformationType: \"Direct\"\n"
        "      Normals: *9 { a: 0,-1,0,0,-1,0,0,-1,0 }\n"
        "    }\n"
        "    LayerElementUV: 0 {\n"
        "      MappingInformationType: \"ByPolygonVertex\"\n"
        "      ReferenceInformationType: \"IndexToDirect\"\n"
        "      UV: *6 { a: 0,0,1,0,0,1 }\n"
        "      UVIndex: *3 { a: 2,1,0 }\n"
        "    }\n"
        "  }\n";
}

const Models::Vertex *findVertex(
    const Models::MeshData& mesh,
    float x,
    float y,
    float z)
{
    for (const auto& vertex : mesh.vertices) {
        if (near(vertex.position.x, x) && near(vertex.position.y, y) && near(vertex.position.z, z)) {
            return &vertex;
        }
    }
    return nullptr;
}

void testUnitsAxesHierarchyAndUvs()
{
    const std::string path = writeFixture(
        "horse-fbx-unit-axis",
        globalSettings(1.0, 2, 1, 1, -1, 0, 1) +
        "Objects: {\n" +
        triangleGeometry() +
        "  Model: 2, \"Model::Tri\", \"Mesh\" {\n"
        "    Properties70: {\n"
        "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n"
        "      P: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",0,0,0\n"
        "      P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",100,100,100\n"
        "    }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "}\n"
    );

    Models::Fbx::Document document;
    std::string error;
    assert(Models::Fbx::load(path, &document, &error));
    assert(error.empty());
    assert(document.parts.size() == 1u);

    const Models::MeshData& mesh = document.parts[0].mesh;
    assert(mesh.vertices.size() == 3u);
    assert(mesh.indices.size() == 3u);

    // Handedness conversion may reverse emitted corner order, so verify
    // source-corner associations by canonical position rather than array slot.
    const Models::Vertex *origin = findVertex(mesh, 0.0f, 0.0f, 0.0f);
    const Models::Vertex *right = findVertex(mesh, 1.0f, 0.0f, 0.0f);
    const Models::Vertex *up = findVertex(mesh, 0.0f, 1.0f, 0.0f);
    assert(origin != nullptr);
    assert(right != nullptr);
    assert(up != nullptr);

    // IndexToDirect must stay attached to its source polygon corner.
    assert(near(origin->uv.x, 0.0f));
    assert(near(origin->uv.y, 1.0f));
    assert(near(right->uv.x, 1.0f));
    assert(near(right->uv.y, 0.0f));
    assert(near(up->uv.x, 0.0f));
    assert(near(up->uv.y, 0.0f));
}

void testRotationPivot()
{
    const std::string path = writeFixture(
        "horse-fbx-pivot",
        globalSettings(100.0, 1, 1, 2, -1, 0, 1) +
        "Objects: {\n"
        "  Geometry: 10, \"Geometry::PivotTri\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 2,0,0,2,1,0,1,0,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 11, \"Model::PivotTri\", \"Mesh\" {\n"
        "    Properties70: {\n"
        "      P: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",0,0,90\n"
        "      P: \"RotationOrder\", \"enum\", \"\", \"\",0\n"
        "      P: \"RotationPivot\", \"Vector3D\", \"Vector\", \"\",1,0,0\n"
        "    }\n"
        "  }\n"
        "}\n"
        "Connections: { C: \"OO\",10,11 }\n"
    );

    Models::Fbx::Document document;
    std::string error;
    assert(Models::Fbx::load(path, &document, &error));
    assert(error.empty());
    assert(document.parts.size() == 1u);
    const auto& vertex = document.parts[0].mesh.vertices[0];
    assert(near(vertex.position.x, 1.0f));
    assert(near(vertex.position.y, 1.0f));
}

void testUnsupportedRotationOrderFails()
{
    const std::string path = writeFixture(
        "horse-fbx-spheric-order",
        globalSettings(100.0, 1, 1, 2, -1, 0, 1) +
        "Objects: {\n" +
        triangleGeometry() +
        "  Model: 2, \"Model::Unsupported\", \"Mesh\" {\n"
        "    Properties70: { P: \"RotationOrder\", \"enum\", \"\", \"\",6 }\n"
        "  }\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n"
    );

    Models::Fbx::Document document;
    std::string error;
    assert(!Models::Fbx::load(path, &document, &error));
    assert(error.find("rotation") != std::string::npos || error.find("Rotation") != std::string::npos);
}

} // namespace

int main()
{
    testUnitsAxesHierarchyAndUvs();
    testRotationPivot();
    testUnsupportedRotationOrderFails();
    return 0;
}
