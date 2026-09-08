#include "Models/Formats/Fbx.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <string>

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool near(float a,float b,float epsilon=2.0e-4f)
{
    return std::abs(a-b)<=epsilon;
}

Models::Vec3 rotateX(Models::Vec3 p,float degrees)
{
    const float r=degrees*(kPi/180.0f),c=std::cos(r),s=std::sin(r);
    return {p.x,c*p.y-s*p.z,s*p.y+c*p.z};
}

Models::Vec3 rotateY(Models::Vec3 p,float degrees)
{
    const float r=degrees*(kPi/180.0f),c=std::cos(r),s=std::sin(r);
    return {c*p.x+s*p.z,p.y,-s*p.x+c*p.z};
}

Models::Vec3 rotateZ(Models::Vec3 p,float degrees)
{
    const float r=degrees*(kPi/180.0f),c=std::cos(r),s=std::sin(r);
    return {c*p.x-s*p.y,s*p.x+c*p.y,p.z};
}

Models::Vec3 rotateAxis(Models::Vec3 p,char axis,Models::Vec3 angles)
{
    if (axis=='X') return rotateX(p,angles.x);
    if (axis=='Y') return rotateY(p,angles.y);
    return rotateZ(p,angles.z);
}

std::array<char,3> axesFor(int order)
{
    switch(order) {
        case 0: return {'X','Y','Z'};
        case 1: return {'X','Z','Y'};
        case 2: return {'Y','Z','X'};
        case 3: return {'Y','X','Z'};
        case 4: return {'Z','X','Y'};
        default:return {'Z','Y','X'};
    }
}

std::string writeFixture(int order)
{
    const std::string path="/tmp/horse-fbx-order-"+std::to_string(order)+".fbx";
    std::ofstream file(path,std::ios::binary);
    file <<
        "; FBX 7.4.0 project file\n"
        "GlobalSettings: { Properties70: {\n"
        " P: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
        " P: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        " P: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        " P: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        " P: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
        " P: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        " P: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "} }\n"
        "Objects: {\n"
        " Geometry: 1, \"Geometry::Order\", \"Mesh\" {\n"
        "  Vertices: *9 { a: 1,2,3,2,2,3,1,3,3 }\n"
        "  PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        " }\n"
        " Model: 2, \"Model::Order\", \"Mesh\" { Properties70: {\n"
        "  P: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",20,30,40\n"
        "  P: \"RotationOrder\", \"enum\", \"\", \"\"," << order << "\n"
        " } }\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n";
    return path;
}

} // namespace

int main()
{
    const Models::Vec3 angles{20.0f,30.0f,40.0f};
    for (int order=0;order<6;++order) {
        Models::Vec3 expected{1.0f,2.0f,3.0f};
        for (char axis:axesFor(order)) expected=rotateAxis(expected,axis,angles);

        Models::Fbx::Document document;
        std::string error;
        assert(Models::Fbx::load(writeFixture(order),&document,&error));
        assert(error.empty());
        assert(document.parts.size()==1u);
        const Models::Vec3 actual=document.parts[0].mesh.vertices[0].position;
        assert(near(actual.x,expected.x));
        assert(near(actual.y,expected.y));
        assert(near(actual.z,expected.z));
    }
    return 0;
}
