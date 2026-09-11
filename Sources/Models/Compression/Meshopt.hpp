#ifndef HORSE_MODELS_COMPRESSION_MESHOPT_HPP
#define HORSE_MODELS_COMPRESSION_MESHOPT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression {

enum class MeshoptMode : std::uint8_t {
    Attributes,
    Triangles,
    Indices,
};

enum class MeshoptFilter : std::uint8_t {
    None,
    Octahedral,
    Quaternion,
    Exponential,
};

bool decodeMeshopt(
    const std::uint8_t *data,
    std::size_t size,
    std::size_t count,
    std::size_t stride,
    MeshoptMode mode,
    MeshoptFilter filter,
    std::vector<std::uint8_t> *output,
    std::string *error = nullptr
);

} // namespace Models::Compression

#endif
