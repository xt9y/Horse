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
    const std::string source = read("Sources/Renderer/Rasterizer/Rasterizer.cpp");
    const std::string shaders = read("Sources/Renderer/Rasterizer/RasterizerShaders.hpp");

    assert(shaders.find("#version 120") != std::string::npos);
    assert(shaders.find("sampler3D uGi0") != std::string::npos);
    assert(source.find("uploadGlobalIllumination") != std::string::npos);
    assert(source.find("GLModern.glTexImage3D") != std::string::npos);
    assert(source.find("GlobalIllumination::sample(gi") == std::string::npos);

    assert(source.find("renderPointShadowMaps") != std::string::npos);
    assert(shaders.find("uShadow0") != std::string::npos);
    assert(shaders.find("shadowVisibility") != std::string::npos);
    assert(source.find("glCopyTexSubImage2D") != std::string::npos);
    assert(shaders.find("discard") != std::string::npos);
    return 0;
}
