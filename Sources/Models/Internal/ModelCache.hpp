#ifndef HORSE_MODELS_INTERNAL_MODEL_CACHE_HPP
#define HORSE_MODELS_INTERNAL_MODEL_CACHE_HPP

#include "Models/Formats/Registry.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Models::Internal::ModelCache {

struct Payload {
    std::vector<std::uint8_t> bytes;
};

bool load(
    const std::string& source,
    Formats::Document *document,
    std::string *error = nullptr
);

bool encode(
    const std::string& source,
    const Formats::Document& document,
    Payload *payload,
    std::string *error = nullptr
);

void writeAsync(const std::string& source, Payload payload);
std::filesystem::path pathFor(const std::string& source);

} // namespace Models::Internal::ModelCache

#endif
