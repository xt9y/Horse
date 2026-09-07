#include "Models/Formats/Fbx.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <string>

int main(int argc, char **argv)
{
    assert(argc == 2);
    Models::Fbx::Document document;
    std::string error;
    assert(Models::Fbx::load(argv[1], &document, &error));
    assert(error.empty());
    assert(!document.parts.empty());

    std::size_t triangles = 0u;
    for (const auto& part : document.parts) {
        assert(part.mesh.indices.size() % 3u == 0u);
        triangles += part.mesh.indices.size() / 3u;
        for (const auto& vertex : part.mesh.vertices) {
            assert(std::isfinite(vertex.position.x));
            assert(std::isfinite(vertex.position.y));
            assert(std::isfinite(vertex.position.z));
            assert(std::isfinite(vertex.normal.x));
            assert(std::isfinite(vertex.normal.y));
            assert(std::isfinite(vertex.normal.z));
            assert(std::isfinite(vertex.uv.x));
            assert(std::isfinite(vertex.uv.y));
        }
    }
    assert(triangles > 0u);
    return 0;
}
