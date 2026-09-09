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
    const std::size_t sync = source.find("bool syncScene(");
    assert(sync != std::string::npos);
    const std::size_t material_index = source.find("auto materialIndex", sync);
    assert(material_index != std::string::npos);
    const std::size_t alpha_priority = source.find("meaningful_alpha", sync);
    assert(alpha_priority != std::string::npos);
    assert(alpha_priority < material_index);
}

int main()
{
    check(read("Sources/Renderer/PathTracer/PathTracer.cpp"));
    check(read("Sources/Renderer/PathTracer/PathTracerMetal.cpp"));
    return 0;
}
