#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models {
namespace {

std::vector<TextureAsset>& assets()
{
    static std::vector<TextureAsset> values;
    return values;
}

std::unordered_map<std::string, TextureHandle>& cache()
{
    static std::unordered_map<std::string, TextureHandle> values;
    return values;
}

std::string normalizedPath(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::path(path), error);
    return (error ? std::filesystem::path(path) : absolute)
        .lexically_normal()
        .string();
}

TextureHandle store(
    std::string key,
    Images::Image image)
{
    const TextureHandle handle = static_cast<TextureHandle>(assets().size());
    assets().push_back({key, std::move(image)});
    cache().emplace(std::move(key), handle);
    return handle;
}

} // namespace

TextureHandle loadTexture(const std::string& path, std::string *error)
{
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = "empty texture path";
        return INVALID_TEXTURE;
    }

    const std::string key = normalizedPath(path);
    const auto found = cache().find(key);
    if (found != cache().end()) return found->second;

    Images::Image image;
    if (!Images::load(key, &image, error)) return INVALID_TEXTURE;
    return store(key, std::move(image));
}

TextureHandle loadTextureWithOpacity(
    const std::string& color_path,
    const std::string& opacity_path,
    std::string *error)
{
    if (error) error->clear();
    if (color_path.empty() || opacity_path.empty()) {
        if (error) *error = "empty color or opacity texture path";
        return INVALID_TEXTURE;
    }

    const std::string color_key = normalizedPath(color_path);
    const std::string opacity_key = normalizedPath(opacity_path);
    const std::string key = color_key + "\n@opacity:" + opacity_key;
    const auto found = cache().find(key);
    if (found != cache().end()) return found->second;

    Images::Image color;
    std::string color_error;
    if (!Images::load(color_key, &color, &color_error)) {
        if (error) *error = color_error;
        return INVALID_TEXTURE;
    }

    Images::Image opacity;
    std::string opacity_error;
    if (!Images::load(opacity_key, &opacity, &opacity_error)) {
        if (error) *error = opacity_error;
        return INVALID_TEXTURE;
    }

    if (color.width <= 0 || color.height <= 0 || color.rgba.empty()
        || opacity.width <= 0 || opacity.height <= 0 || opacity.rgba.empty()) {
        if (error) *error = "invalid color or opacity texture image";
        return INVALID_TEXTURE;
    }

    for (int y = 0; y < color.height; ++y) {
        const int opacity_y = std::clamp(
            static_cast<int>(
                (static_cast<long long>(y) * static_cast<long long>(opacity.height)) /
                static_cast<long long>(color.height)
            ),
            0,
            opacity.height - 1
        );
        for (int x = 0; x < color.width; ++x) {
            const int opacity_x = std::clamp(
                static_cast<int>(
                    (static_cast<long long>(x) * static_cast<long long>(opacity.width)) /
                    static_cast<long long>(color.width)
                ),
                0,
                opacity.width - 1
            );

            const std::size_t color_offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(color.width) +
                 static_cast<std::size_t>(x)) * 4u;
            const std::size_t opacity_offset =
                (static_cast<std::size_t>(opacity_y) * static_cast<std::size_t>(opacity.width) +
                 static_cast<std::size_t>(opacity_x)) * 4u;
            if (color_offset + 3u >= color.rgba.size()
                || opacity_offset + 2u >= opacity.rgba.size()) {
                continue;
            }

            const unsigned int mask =
                (static_cast<unsigned int>(opacity.rgba[opacity_offset + 0u]) +
                 static_cast<unsigned int>(opacity.rgba[opacity_offset + 1u]) +
                 static_cast<unsigned int>(opacity.rgba[opacity_offset + 2u])) / 3u;
            const unsigned int source_alpha = color.rgba[color_offset + 3u];
            color.rgba[color_offset + 3u] = static_cast<std::uint8_t>(
                (source_alpha * mask + 127u) / 255u
            );
        }
    }

    color.meaningful_alpha = true;
    return store(key, std::move(color));
}

TextureHandle loadTextureMemory(
    const std::string& cache_key,
    const void *data,
    std::size_t size,
    std::string *error)
{
    if (error) error->clear();
    if (cache_key.empty()) {
        if (error) *error = "empty embedded texture cache key";
        return INVALID_TEXTURE;
    }

    const auto found = cache().find(cache_key);
    if (found != cache().end()) return found->second;

    Images::Image image;
    if (!Images::loadMemory(data, size, &image, error)) return INVALID_TEXTURE;
    return store(cache_key, std::move(image));
}

const TextureAsset *texture(TextureHandle handle)
{
    return handle < assets().size() ? &assets()[handle] : nullptr;
}

void clearTextureCache()
{
    cache().clear();
    assets().clear();
}

} // namespace Models
