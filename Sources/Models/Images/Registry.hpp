#ifndef HORSE_MODELS_IMAGES_REGISTRY_HPP
#define HORSE_MODELS_IMAGES_REGISTRY_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Models::Images {

using Matcher = bool (*)(const std::uint8_t *, std::size_t);
using DecoderFunction = bool (*)(const std::uint8_t *, std::size_t, Image *, std::string *);

struct Decoder {
    std::string extension;
    Matcher matches = nullptr;
    DecoderFunction decode = nullptr;
};

bool registerDecoder(std::string extension, Matcher matches, DecoderFunction decode);
const Decoder *decoderFor(std::string_view extension);
const Decoder *decoderFor(const std::uint8_t *data, std::size_t size);

class Registration {
public:
    Registration(const char *extension, Matcher matches, DecoderFunction decode);
};

} // namespace Models::Images

#endif
