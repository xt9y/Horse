#ifndef HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_COMPLETE_HPP
#define HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_COMPLETE_HPP
#include "Draco.hpp"
namespace Models::Compression {
bool decodeDracoEdgeBreakerComplete(const std::uint8_t *data, std::size_t size, DracoMesh *mesh, std::string *error = nullptr);
}
#endif
