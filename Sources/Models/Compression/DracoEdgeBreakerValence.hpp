#ifndef HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_VALENCE_HPP
#define HORSE_MODELS_COMPRESSION_DRACO_EDGEBREAKER_VALENCE_HPP
#include "Draco.hpp"
namespace Models::Compression {
bool decodeDracoEdgeBreakerValence(const std::uint8_t*,std::size_t,DracoMesh*,std::string* = nullptr);
}
#endif
