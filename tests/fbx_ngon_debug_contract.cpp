#include "Models/Formats/Fbx.hpp"

#include <cstdio>
#include <fstream>
#include <string>

int main()
{
    const std::string path = "/tmp/horse-fbx-material-debug.fbx";
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
        "}\n";
    file.close();

    Models::Fbx::Document document;
    std::string error;
    if (!Models::Fbx::load(path, &document, &error)) {
        std::fprintf(stderr, "material import failed: %s\n", error.c_str());
        return 1;
    }
    if (document.parts.size() != 2u) {
        std::fprintf(stderr, "material output parts=%zu\n", document.parts.size());
        return 2;
    }
    std::fprintf(stderr, "parts: %s / %s\n", document.parts[0].material.name.c_str(), document.parts[1].material.name.c_str());
    return (document.parts[0].material.name == "Red" && document.parts[1].material.name == "Green") ? 0 : 3;
}
