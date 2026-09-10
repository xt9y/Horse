#include "Models/Images/Registry.hpp"

#include <algorithm>
#include <cctype>

namespace Models::Images {
namespace {

std::vector<Decoder>& decoders()
{
    static std::vector<Decoder> values;
    return values;
}

std::string normalize(std::string value)
{
    if (!value.empty() && value.front() != '.') value.insert(value.begin(), '.');
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    return value;
}

} // namespace

bool registerDecoder(std::string extension, Matcher matches, DecoderFunction decode)
{
    if (!matches || !decode) return false;
    extension = normalize(std::move(extension));
    if (extension.empty()) return false;

    for (Decoder& value : decoders()) {
        if (value.extension != extension) continue;
        value.matches = matches;
        value.decode = decode;
        return true;
    }
    decoders().push_back(Decoder{std::move(extension), matches, decode});
    return true;
}

const Decoder *decoderFor(std::string_view extension)
{
    const std::string key = normalize(std::string(extension));
    for (const Decoder& value : decoders()) {
        if (value.extension == key) return &value;
    }
    return nullptr;
}

const Decoder *decoderFor(const std::uint8_t *data, std::size_t size)
{
    for (const Decoder& value : decoders()) {
        if (value.matches && value.matches(data, size)) return &value;
    }
    return nullptr;
}

Registration::Registration(const char *extension, Matcher matches, DecoderFunction decode)
{
    if (extension) registerDecoder(extension, matches, decode);
}

} // namespace Models::Images
