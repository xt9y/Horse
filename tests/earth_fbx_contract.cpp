#include "Models/Formats/Fbx.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

int main(int argc, char **argv)
{
    assert(argc == 2);
    Models::Fbx::Document document;
    std::string error;
    assert(Models::Fbx::load(argv[1], &document, &error));
    assert(error.empty());
    assert(document.parts.size() == 3u);

    std::size_t triangles = 0u;
    bool any_texture = false;
    float minimum = std::numeric_limits<float>::max();
    float maximum = 0.0f;

    for (const auto& part : document.parts) {
        assert(!part.mesh.vertices.empty());
        assert(part.mesh.indices.size() % 3u == 0u);
        triangles += part.mesh.indices.size() / 3u;
        any_texture |= part.material.diffuse_texture != Models::INVALID_TEXTURE;

        for (const auto& vertex : part.mesh.vertices) {
            assert(std::isfinite(vertex.position.x));
            assert(std::isfinite(vertex.position.y));
            assert(std::isfinite(vertex.position.z));
            assert(std::isfinite(vertex.normal.x));
            assert(std::isfinite(vertex.normal.y));
            assert(std::isfinite(vertex.normal.z));
            assert(std::isfinite(vertex.uv.x));
            assert(std::isfinite(vertex.uv.y));

            const float radius = std::sqrt(
                vertex.position.x * vertex.position.x +
                vertex.position.y * vertex.position.y +
                vertex.position.z * vertex.position.z
            );
            minimum = std::min(minimum, radius);
            maximum = std::max(maximum, radius);
        }
    }

    assert(triangles > 0u);
    assert(!any_texture);

    // This FBX declares centimeter units and a 100x root scale. Correct
    // canonicalization therefore yields an engine-scale globe, not a
    // roughly hundred-unit object.
    assert(minimum > 0.1f);
    assert(maximum < 10.0f);
    return 0;
}
