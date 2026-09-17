#include "Models/Formats/Registry.hpp"

#include "Models/Internal/ModelCache.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

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

bool cacheEligible(const std::string& path)
{
    const std::string extension = normalize(std::filesystem::path(path).extension().string());
    return extension == ".gltf" || extension == ".glb";
}

bool cachedLoad(const std::string& path, Document *output, std::string *error)
{
    if (!output) return false;
    const std::string extension = normalize(std::filesystem::path(path).extension().string());
    const auto found = loaders().find(extension);
    if (found == loaders().end() || !found->second) return false;

    if (cacheEligible(path)) {
        std::string cache_error;
        if (Internal::ModelCache::load(path, output, &cache_error)) {
            if (error) error->clear();
            return true;
        }
    }

    if (!found->second(path, output, error)) return false;

    if (cacheEligible(path)) {
        Internal::ModelCache::Payload payload;
        std::string cache_error;
        if (Internal::ModelCache::encode(path, *output, &payload, &cache_error))
            Internal::ModelCache::writeAsync(path, std::move(payload));
    }
    return true;
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
    return found == loaders().end() ? nullptr : cachedLoad;
}

Registration::Registration(const char *extension, Loader loader)
{
    if (extension) registerLoader(extension, loader);
}

} // namespace Models::Formats
