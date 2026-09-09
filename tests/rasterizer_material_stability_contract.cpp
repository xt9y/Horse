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
    const std::string shader = read("Sources/Renderer/Rasterizer/RasterizerShaders.hpp");
    const std::string material = read("Sources/Models/Core/Material.cpp");
    const std::string obj = read("Sources/Models/Formats/Obj.cpp");

    assert(shader.find("bounded_irradiance") != std::string::npos);
    assert(shader.find("albedo * (direct + indirect)") == std::string::npos);

    assert(material.find("failed to load diffuse texture") != std::string::npos);
    assert(obj.find("failed to load OBJ material library") != std::string::npos);
    return 0;
}
