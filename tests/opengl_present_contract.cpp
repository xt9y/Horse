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
    const std::string renderer = read("Sources/Renderer/Renderer.cpp");
    const std::size_t present = renderer.find("present(output);");
    const std::size_t swap = renderer.find("Display.updateNoMessages();");

    assert(present != std::string::npos);
    assert(swap != std::string::npos);
    assert(present < swap);
    assert(renderer.find("output.api == Internal::GraphicsApi::OpenGL") != std::string::npos);
    return 0;
}
