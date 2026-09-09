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
    const std::string source = read("Sources/Renderer/GlobalIllumination.cpp");

    assert(source.find("kRayBudgetPerFrame") != std::string::npos);
    assert(source.find("ray_cursor") != std::string::npos);
    assert(source.find("probe_accumulator") != std::string::npos);
    assert(source.find("advanceProbeRay") != std::string::npos);
    assert(source.find("while (ray_budget > 0u") != std::string::npos);
    return 0;
}
