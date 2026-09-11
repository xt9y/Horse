#ifndef HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_ATTRIBUTES_HPP
#define HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_ATTRIBUTES_HPP

#include "Draco.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models::Compression {

struct DracoEdgeBreakerTopology {
    std::uint32_t position_vertex_count = 0u;
    std::vector<std::uint32_t> corner_vertices;
    std::vector<std::int32_t> opposite;
    std::vector<std::vector<std::uint8_t>> seams;
};

bool dracoEdgeBreakerCornerDomains(
    const DracoEdgeBreakerTopology& topology,
    std::size_t seam_index,
    std::vector<std::uint32_t> *corner_domains,
    std::uint32_t *domain_count
);

bool decodeDracoEdgeBreakerAttributes(
    const std::uint8_t *data,
    std::size_t size,
    const DracoEdgeBreakerTopology& topology,
    DracoMesh *mesh,
    std::string *error = nullptr
);

} // namespace Models::Compression

#endif
