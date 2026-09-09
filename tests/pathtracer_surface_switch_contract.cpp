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
    const std::string header = read("Sources/Renderer/PathTracer/PathTracer.hpp");
    const std::string metal = read("Sources/Renderer/PathTracer/PathTracerMetal.cpp");

    assert(header.find("setPresentationActive") != std::string::npos);
    assert(header.find("presentationActive") != std::string::npos);
    assert(metal.find("lwmglSurfaceDetach") != std::string::npos);
    assert(metal.find("lwmglSurfaceAttach") != std::string::npos);
    assert(metal.find("presentation_active") != std::string::npos);
    assert(metal.find("if (impl_->presentation_active)") != std::string::npos);
    return 0;
}
