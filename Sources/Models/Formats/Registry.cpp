#include "Models/Formats/Registry.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Models::Formats {
namespace {

std::string normalize(std::string value)
{
    if (!value.empty() && value.front() != '.') value.insert(value.begin(), '.');
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    return value;
}

std::unordered_map<std::string, Loader>& loaders()
{
    static std::unordered_map<std::string, Loader> values;
    return values;
}

} // namespace

bool registerLoader(std::string extension, Loader loader)
{
    if (!loader) return false;
    extension = normalize(std::move(extension));
    if (extension.empty()) return false;
    loaders()[extension] = loader;
    return true;
}

Loader loaderFor(std::string_view extension)
{
    const auto found = loaders().find(normalize(std::string(extension)));
    return found == loaders().end() ? nullptr : found->second;
}

Registration::Registration(const char *extension, Loader loader)
{
    if (extension) registerLoader(extension, loader);
}

} // namespace Models::Formats
