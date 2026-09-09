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
    const std::string source = read("Sources/Renderer/FontPassOpenGL.cpp");

    assert(source.find("texture(uAtlas, vUv)") != std::string::npos);
    assert(source.find("texture(uAtlas, vec2(vUv.x, 1.0 - vUv.y))") == std::string::npos);
    assert(source.find("glTexCoord2f(vertex.uv_depth[0], vertex.uv_depth[1]);") != std::string::npos);
    assert(source.find("glTexCoord2f(vertex.uv_depth[0], 1.0f - vertex.uv_depth[1]);") == std::string::npos);

    return 0;
}
