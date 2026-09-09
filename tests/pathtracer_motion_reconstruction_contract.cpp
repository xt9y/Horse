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

static void check(const std::string& source)
{
    assert(source.find("reconstructSparseSample") != std::string::npos);
    assert(source.find("weight_sum") != std::string::npos);
    assert(source.find("camera_moving") != std::string::npos || source.find("uCameraMoving") != std::string::npos);
}

int main()
{
    check(read("Sources/Renderer/PathTracer/PathTracerWorldFastShaders.hpp"));
    check(read("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp"));
    return 0;
}
