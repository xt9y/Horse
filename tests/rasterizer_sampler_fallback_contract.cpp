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

    assert(source.find("fallbackTexture") != std::string::npos);
    assert(source.find("const std::uint8_t white[4]") != std::string::npos);
    assert(source.find("texture_id != 0u ? texture_id : fallbackTexture()") != std::string::npos);
    assert(source.find("setInt(main_uniforms.has_texture, has_texture ? 1 : 0)") != std::string::npos);
    assert(source.find("setInt(shadow_uniforms.has_texture, has_texture ? 1 : 0)") != std::string::npos);
    return 0;
}
