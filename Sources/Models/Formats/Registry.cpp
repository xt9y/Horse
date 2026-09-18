#include "Models/Formats/Registry.hpp"

#include "Core/Jobs/Jobs.hpp"

#include "Models/Internal/ModelCache.hpp"
#include "Models/Internal/StagedModel.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

std::atomic<std::uint64_t>& sourceLoads()
{
    static std::atomic<std::uint64_t> value {0u};
    return value;
}

bool cacheEligible(const std::string& path)
{
    const std::string extension = normalize(std::filesystem::path(path).extension().string());
    return extension == ".gltf" || extension == ".glb";
}

void queueCacheBuild(const std::string& path)
{
    if (!cacheEligible(path)) return;

    static std::mutex mutex;
    static std::unordered_set<std::string> queued;
    {
        std::lock_guard lock(mutex);
        if (!queued.insert(path).second) return;
    }

    if (!Core::Jobs::trySubmit(
            [path] {
                Internal::StagedModel staged;
                std::string error;
                (void)stage(path, &staged, &error);
            },
            [path] {
                std::lock_guard lock(mutex);
                queued.erase(path);
            }))
    {
        std::lock_guard lock(mutex);
        queued.erase(path);
    }
}

bool cachedLoad(const std::string& path, Document *output, std::string *error)
{
    if (!output) return false;
    Internal::pumpTextureResources();

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

    Internal::TextureImportScope texture_import;
    sourceLoads().fetch_add(1u, std::memory_order_relaxed);
    if (!found->second(path, output, error)) return false;

    queueCacheBuild(path);
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

bool stage(const std::string& path, Internal::StagedModel *output, std::string *error)
{
    if (error) error->clear();
    if (!output) {
        if (error) *error = "null staged model output";
        return false;
    }

    const std::string extension = normalize(std::filesystem::path(path).extension().string());
    const auto found = loaders().find(extension);
    if (found == loaders().end() || !found->second) {
        if (error) *error = "unsupported model format: " + extension;
        return false;
    }

    *output = Internal::StagedModel{};
    Internal::StagingScope staging(*output);
    Internal::TextureImportScope texture_import;

    if (cacheEligible(path)) {
        std::string cache_error;
        if (Internal::ModelCache::load(path, &output->document, &cache_error)) {
            if (error) error->clear();
            return true;
        }
    }

    sourceLoads().fetch_add(1u, std::memory_order_relaxed);
    if (!found->second(path, &output->document, error)) return false;

    if (cacheEligible(path)) {
        Internal::ModelCache::Payload payload;
        std::string cache_error;
        if (Internal::ModelCache::encode(path, output->document, &payload, &cache_error))
            Internal::ModelCache::writeAsync(path, std::move(payload));
    }
    return true;
}

std::uint64_t sourceLoaderInvocationCount()
{
    return sourceLoads().load(std::memory_order_relaxed);
}

Registration::Registration(const char *extension, Loader loader)
{
    if (extension) registerLoader(extension, loader);
}

} // namespace Models::Formats
