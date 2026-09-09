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
    assert(source.find("return float4(0.0f, 0.0f, 0.0f, 1.0f)") == std::string::npos);
    assert(source.find("outColor = vec4(0.0, 0.0, 0.0, 1.0)") == std::string::npos);
}

int main()
{
    check(read("Sources/Renderer/PathTracer/PathTracerPresentShaders.hpp"));
    check(read("Sources/Renderer/PathTracer/PathTracerMetalShaders.hpp"));
    return 0;
}
