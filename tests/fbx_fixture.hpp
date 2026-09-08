#ifndef HORSE_TESTS_FBX_FIXTURE_HPP
#define HORSE_TESTS_FBX_FIXTURE_HPP

#include "Models/Formats/Fbx.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

namespace HorseFbxTest {

inline std::string canonicalSettings()
{
    return
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
        "}\n";
}

inline std::string writeFixture(const std::string& name, const std::string& body)
{
    const std::string path = "/tmp/" + name + ".fbx";
    std::ofstream file(path, std::ios::binary);
    assert(file.good());
    file << "; FBX 7.4.0 project file\n" << canonicalSettings() << body;
    file.close();
    return path;
}

inline Models::Fbx::Document load(const std::string& path)
{
    Models::Fbx::Document document;
    std::string error;
    if (!Models::Fbx::load(path, &document, &error)) {
        std::fprintf(stderr, "FBX fixture import failed for %s: %s\n", path.c_str(), error.c_str());
        assert(false);
    }
    assert(error.empty());
    return document;
}

inline bool near(float a, float b, float epsilon = 1.0e-4f)
{
    const float delta = a > b ? a - b : b - a;
    return delta <= epsilon;
}

} // namespace HorseFbxTest

#endif
