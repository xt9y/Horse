#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

static std::string read(const char *path)
{
    std::ifstream file(path);
    assert(file.good());
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main()
{
    const std::string material = read("Sources/Models/Core/Material.cpp");
    const std::string texture_h = read("Sources/Models/Core/Texture.hpp");
    const std::string texture_cpp = read("Sources/Models/Core/Texture.cpp");

    assert(texture_h.find("loadTextureWithOpacity") != std::string::npos);
    assert(texture_cpp.find("loadTextureWithOpacity") != std::string::npos);
    assert(texture_cpp.find("meaningful_alpha = true") != std::string::npos);
    assert(material.find("loadTextureWithOpacity") != std::string::npos);
    assert(material.find("opacity_texture_path") != std::string::npos);
    return 0;
}
