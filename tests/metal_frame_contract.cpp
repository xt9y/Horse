#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

int main()
{
    std::ifstream file("Sources/Renderer/PathTracer/PathTracerMetal.cpp");
    std::ostringstream stream;
    stream << file.rdbuf();
    const std::string source = stream.str();

    assert(source.find("bool PathTracer::renderScene") != std::string::npos);
    assert(source.find("void PathTracer::present") != std::string::npos);
    assert(source.find("dispatchAndCompose") != std::string::npos);
    assert(source.find("Metal.setTexture(command, primary_depth, 17u)") != std::string::npos);
    assert(source.find("output.depth = Internal::DepthSource::LinearTexture") != std::string::npos);
    return 0;
}
