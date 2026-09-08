#include "Models/Formats/Fbx.hpp"

#include <cstdio>
#include <fstream>
#include <string>

int main()
{
    const std::string path = "/tmp/horse-fbx-geometric-debug.fbx";
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
        "  Geometry: 1, \"Geometry::Child\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::Parent\", \"Null\" {\n"
        "    Properties70: { P: \"GeometricTranslation\", \"Vector3D\", \"Vector\", \"\",10,0,0 }\n"
        "  }\n"
        "  Model: 3, \"Model::Child\", \"Mesh\" {\n"
        "    Properties70: { P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0 }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,3\n"
        "  C: \"OO\",3,2\n"
        "}\n";
    file.close();

    Models::Fbx::Document document;
    std::string error;
    if (!Models::Fbx::load(path, &document, &error)) {
        std::fprintf(stderr, "geometric import failed: %s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr, "geometric import parts=%zu\n", document.parts.size());
    return 0;
}
