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

    assert(shader.find("pow(max(texel.rgb, vec3(0.0)), vec3(2.2))") != std::string::npos);
    assert(shader.find("mapped = linear_color / (vec3(1.0) + linear_color)") != std::string::npos);
    assert(shader.find("pow(mapped, vec3(1.0 / 2.2))") != std::string::npos);

    assert(shader.find("receiver_position") != std::string::npos);
    assert(shader.find("normal_bias") != std::string::npos);
    return 0;
}
