#include "Models/Formats/Fbx.hpp"

#include <cstdio>
#include <fstream>
#include <string>

int main()
{
    const std::string path = "/tmp/horse-fbx-ngon-debug.fbx";
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
        "  Geometry: 1, \"Geometry::Concave\", \"Mesh\" {\n"
        "    Vertices: *15 { a: 0,0,0,2,0,0,2,2,0,1,1,0,0,2,0 }\n"
        "    PolygonVertexIndex: *5 { a: 0,1,2,3,-5 }\n"
        "  }\n"
        "  Model: 2, \"Model::Concave\", \"Mesh\" {}\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n";
    file.close();

    Models::Fbx::Document document;
    std::string error;
    if (!Models::Fbx::load(path, &document, &error)) {
        std::fprintf(stderr, "ngon import failed: %s\n", error.c_str());
        return 1;
    }
    if (document.parts.size() != 1u || document.parts[0].mesh.indices.size() != 9u) {
        std::fprintf(stderr, "ngon wrong output: parts=%zu indices=%zu\n",
            document.parts.size(),
            document.parts.empty() ? 0u : document.parts[0].mesh.indices.size());
        return 2;
    }
    return 0;
}
