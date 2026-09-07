#ifndef RW_ENGINE_MODELS_FORMATS_FBX_BINARY_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_BINARY_HPP

#include "Models/Formats/FbxDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models::FbxBinary {

bool matches(const std::uint8_t *data, std::size_t size);

bool parse(
    const std::uint8_t *data,
    std::size_t size,
    FbxDocument::RawDocument *out,
    std::string *error = nullptr
);

} // namespace Models::FbxBinary

#endif
