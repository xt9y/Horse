#ifndef HORSE_MODELS_INTERNAL_MODEL_CACHE_CODEC_HPP
#define HORSE_MODELS_INTERNAL_MODEL_CACHE_CODEC_HPP

#include "Models/Formats/Registry.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Models::Internal::ModelCacheCodec {

struct DependencyStamp {
    std::string path;
    std::uint64_t size = 0u;
    std::int64_t modified = 0;
    bool exists = false;
};

bool encode(
    const Formats::Document& document,
    std::vector<DependencyStamp> dependencies,
    std::vector<std::uint8_t> *bytes,
    std::string *error = nullptr
);

bool decode(
    const std::vector<std::uint8_t>& bytes,
    std::vector<DependencyStamp> *dependencies,
    Formats::Document *document,
    std::string *error = nullptr
);

} // namespace Models::Internal::ModelCacheCodec

#endif
