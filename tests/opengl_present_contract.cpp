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
    const std::string rasterizer = read("Sources/Renderer/Rasterizer/Rasterizer.cpp");
    const std::string pathtracer = read("Sources/Renderer/PathTracer/PathTracer.cpp");
    assert(rasterizer.find("Display.updateNoMessages();") != std::string::npos);
    assert(pathtracer.find("Display.updateNoMessages();") != std::string::npos);
    return 0;
}
