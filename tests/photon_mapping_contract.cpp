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
    const std::string photon = read(
        "Sources/Renderer/GlobalIllumination/PhotonMapping/PhotonMap.cpp"
    );

    assert(photon.find("struct DirectionalEmissionDomain") != std::string::npos);
    assert(photon.find("4.0f * kPi") != std::string::npos);
    assert(photon.find("domain.area") != std::string::npos);
    assert(photon.find("emitted_power") != std::string::npos);
    return 0;
}
