#ifndef HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_HPP
#define HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_HPP

#include "Draco.hpp"

namespace Models::Compression {

bool decodeDracoEdgeBreaker(
    const std::uint8_t *data,
    std::size_t size,
    DracoMesh *mesh,
    std::string *error = nullptr
);

} // namespace Models::Compression

#endif
