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
    const std::string source = read("Sources/Renderer/Scenes/Metal/SceneResources.cpp");
    assert(source.find("namespace Renderer::Scenes::Metal") != std::string::npos);

    std::size_t position = 0u;
    while ((position = source.find("Metal.", position)) != std::string::npos) {
        assert(position >= 2u);
        assert(source[position - 1u] == ':');
        assert(source[position - 2u] == ':');
        position += 6u;
    }
    return 0;
}
