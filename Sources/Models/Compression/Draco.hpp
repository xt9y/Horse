#ifndef HORSE_MODELS_COMPRESSION_DRACO_HPP
#define HORSE_MODELS_COMPRESSION_DRACO_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression {

struct DracoAttribute {
    std::uint32_t unique_id = 0u;
    std::uint8_t attribute_type = 0u;
    std::uint8_t data_type = 0u;
    std::uint8_t components = 0u;
    bool normalized = false;
    std::vector<double> values;
};

struct DracoMesh {
    std::uint32_t point_count = 0u;
    std::vector<std::uint32_t> indices;
    std::vector<DracoAttribute> attributes;

    const DracoAttribute *attribute(std::uint32_t unique_id) const;
};

bool decodeDraco(
    const std::uint8_t *data,
    std::size_t size,
    DracoMesh *mesh,
    std::string *error = nullptr
);

} // namespace Models::Compression

#endif
