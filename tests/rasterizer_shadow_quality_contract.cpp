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

    assert(source.find("constexpr int kShadowResolution = 2048;") != std::string::npos);
    assert(source.find("shadow_framebuffer") != std::string::npos);
    assert(source.find("shadow_depth_renderbuffer") != std::string::npos);
    assert(source.find("glFramebufferTexture2D") != std::string::npos);
    assert(source.find("glRenderbufferStorage") != std::string::npos);
    assert(source.find("const int requested_size = kShadowResolution;") != std::string::npos);

    assert(shaders.find("for (int y = -1; y <= 1; ++y)") != std::string::npos);
    assert(shaders.find("for (int x = -1; x <= 1; ++x)") != std::string::npos);
    assert(shaders.find("visibility / 9.0") != std::string::npos);
    return 0;
}
