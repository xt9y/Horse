#include "Models/Formats/Fbx.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

namespace {
bool near(float a, float b, float epsilon = 1.0e-4f) { return std::abs(a-b) <= epsilon; }
}

int main()
{
    const std::string path = "/tmp/horse-fbx-mirror-debug.fbx";
    std::ofstream file(path, std::ios::binary);
    file <<
        "; FBX 7.4.0 project file\n"
        "GlobalSettings: {\n"
        "  Properties70: {\n"
        "    P: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
        "    P: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        "    P: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "    P: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        "    P: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
        "    P: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        "    P: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "  }\n"
        "}\n"
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
        "    Properties70: { P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",-2,1,0.5 }\n"
        "  }\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n";
    file.close();

    Models::Fbx::Document document;
    std::string error;
    if (!Models::Fbx::load(path, &document, &error)) {
        std::fprintf(stderr, "mirror import failed: %s\n", error.c_str());
        return 1;
    }
    if (document.parts.empty()) return 2;
    const auto& mesh = document.parts[0].mesh;
    const float expected_x = -1.0f / std::sqrt(5.0f);
    const float expected_y = 2.0f / std::sqrt(5.0f);
    for (const auto& vertex : mesh.vertices) {
        if (!near(vertex.normal.x, expected_x) || !near(vertex.normal.y, expected_y) || !near(vertex.normal.z, 0.0f)) {
            std::fprintf(stderr, "wrong normal: %.6f %.6f %.6f expected %.6f %.6f 0\n",
                vertex.normal.x, vertex.normal.y, vertex.normal.z, expected_x, expected_y);
            return 3;
        }
    }
    return 0;
}
