#include "Models/Core/Texture.hpp"

#include "Models/Internal/ResourceRevision.hpp"
#include "Models/Internal/StagedModel.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models {
namespace {

std::deque<TextureAsset>& assets()
{
    static std::deque<TextureAsset> values;
    return values;
}

std::vector<std::uint8_t>& readiness()
{
    static std::vector<std::uint8_t> values;
    return values;
}

std::unordered_map<std::string, TextureHandle>& cache()
{
    static std::unordered_map<std::string, TextureHandle> values;
    return values;
}

bool validImage(const Images::Image& image)
{
    if (image.width <= 0 || image.height <= 0) return false;
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height) return false;
    const std::size_t pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4u) return false;
    return image.rgba.size() == pixels * 4u;
}

Images::Image fallbackImage()
{
    Images::Image image;
    image.width = 1;
    image.height = 1;
    image.rgba = {255u, 255u, 255u, 255u};
    image.meaningful_alpha = false;
    return image;
}

TextureHandle store(std::string key, Images::Image image)
{
    const TextureHandle handle = Internal::reserveTexture(key);
    if (handle == INVALID_TEXTURE) return INVALID_TEXTURE;
    if (Internal::textureStorageReady(handle)) return handle;
    return Internal::publishTexture(handle, std::move(image)) ? handle : INVALID_TEXTURE;
}

bool applyOpacity(Images::Image *color, const Images::Image& opacity, std::string *error)
{
    if (!color || !validImage(*color) || !validImage(opacity)) {
        if (error) *error = "invalid color or opacity texture image";
        return false;
    }

    for (int y = 0; y < color->height; ++y) {
        const int opacity_y = std::clamp(
            static_cast<int>(
                (static_cast<long long>(y) * static_cast<long long>(opacity.height)) /
                static_cast<long long>(color->height)
            ),
            0,
            opacity.height - 1
        );
        for (int x = 0; x < color->width; ++x) {
            const int opacity_x = std::clamp(
                static_cast<int>(
                    (static_cast<long long>(x) * static_cast<long long>(opacity.width)) /
                    static_cast<long long>(color->width)
                ),
                0,
                opacity.width - 1
            );

            const std::size_t color_offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(color->width) +
                 static_cast<std::size_t>(x)) * 4u;
            const std::size_t opacity_offset =
                (static_cast<std::size_t>(opacity_y) * static_cast<std::size_t>(opacity.width) +
                 static_cast<std::size_t>(opacity_x)) * 4u;

            const unsigned int mask =
                (static_cast<unsigned int>(opacity.rgba[opacity_offset + 0u]) +
                 static_cast<unsigned int>(opacity.rgba[opacity_offset + 1u]) +
                 static_cast<unsigned int>(opacity.rgba[opacity_offset + 2u])) / 3u;
            const unsigned int source_alpha = color->rgba[color_offset + 3u];
            color->rgba[color_offset + 3u] = static_cast<std::uint8_t>(
                (source_alpha * mask + 127u) / 255u
            );
        }
    }

    color->meaningful_alpha = true;
    return true;
}

TextureHandle deferredAlphaSource(const std::string& key)
{
    static constexpr std::string_view marker = "\n@gltf-alpha:";
    const std::size_t offset = key.rfind(marker);
    if (offset == std::string::npos) return INVALID_TEXTURE;
    return Internal::findTexture(key.substr(0u, offset));
}

} // namespace

std::string Internal::normalizeTexturePath(const std::string& path)
{
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(std::filesystem::path(path), error);
    return (error ? std::filesystem::path(path) : absolute)
        .lexically_normal()
        .string();
}

TextureHandle Internal::findTexture(const std::string& key)
{
    if (Internal::stagingActive()) return Internal::findStagedTexture(key);
    const auto found = cache().find(key);
    return found == cache().end() ? INVALID_TEXTURE : found->second;
}

TextureHandle Internal::reserveTexture(const std::string& key)
{
    if (key.empty()) return INVALID_TEXTURE;
    if (const TextureHandle existing = findTexture(key); existing != INVALID_TEXTURE) return existing;
    if (assets().size() >= static_cast<std::size_t>(INVALID_TEXTURE)) return INVALID_TEXTURE;
    const TextureHandle handle = static_cast<TextureHandle>(assets().size());
    assets().push_back({key, fallbackImage()});
    readiness().push_back(0u);
    cache().emplace(key, handle);
    return handle;
}

std::size_t Internal::textureStorageCount()
{
    return assets().size();
}

bool Internal::textureStorageReady(TextureHandle handle)
{
    return handle < readiness().size() && readiness()[handle] != 0u;
}

bool Internal::publishTexture(TextureHandle handle, Images::Image image)
{
    if (handle >= assets().size() || handle >= readiness().size() || !validImage(image)) return false;
    assets()[handle].image = std::move(image);
    readiness()[handle] = 1u;
    Internal::touchResources();
    return true;
}

TextureHandle loadTexture(const std::string& path, std::string *error)
{
    if (error) error->clear();
    if (path.empty()) {
        if (error) *error = "empty texture path";
        return INVALID_TEXTURE;
    }

    const std::string key = Internal::normalizeTexturePath(path);
    if (Internal::textureImportActive()) return Internal::registerDeferredFile(key);

    const TextureHandle existing = Internal::findTexture(key);
    if (existing != INVALID_TEXTURE && Internal::textureStorageReady(existing)) return existing;

    Images::Image image;
    if (!Images::load(key, &image, error)) return INVALID_TEXTURE;
    if (existing != INVALID_TEXTURE)
        return Internal::publishTexture(existing, std::move(image)) ? existing : INVALID_TEXTURE;
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

    const TextureHandle color = loadTexture(color_path, error);
    if (color == INVALID_TEXTURE) return INVALID_TEXTURE;
    const TextureHandle opacity = loadTexture(opacity_path, error);
    if (opacity == INVALID_TEXTURE) return INVALID_TEXTURE;
    return combineTextureOpacity(color, opacity, error);
}

TextureHandle combineTextureOpacity(
    TextureHandle color,
    TextureHandle opacity,
    std::string *error)
{
    if (error) error->clear();
    const TextureAsset *color_asset = texture(color);
    const TextureAsset *opacity_asset = texture(opacity);
    if (!color_asset || !opacity_asset) {
        if (error) *error = "invalid color or opacity texture handle";
        return INVALID_TEXTURE;
    }

    if (Internal::textureImportActive() ||
        Internal::textureState(color) != Internal::TextureState::Ready ||
        Internal::textureState(opacity) != Internal::TextureState::Ready)
        return Internal::registerDeferredOpacity(color, opacity);

    const std::string key = color_asset->path + "\n@opacity:" + opacity_asset->path;
    if (const TextureHandle found = Internal::findTexture(key); found != INVALID_TEXTURE &&
        Internal::textureStorageReady(found)) return found;

    Images::Image image = color_asset->image;
    if (!applyOpacity(&image, opacity_asset->image, error)) return INVALID_TEXTURE;
    return store(key, std::move(image));
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
    if (!data || size == 0u) {
        if (error) *error = "empty embedded texture data";
        return INVALID_TEXTURE;
    }

    if (Internal::textureImportActive()) {
        const auto *bytes = static_cast<const std::uint8_t *>(data);
        return Internal::registerDeferredMemory(
            cache_key,
            std::vector<std::uint8_t>(bytes, bytes + size)
        );
    }

    const TextureHandle existing = Internal::findTexture(cache_key);
    if (existing != INVALID_TEXTURE && Internal::textureStorageReady(existing)) return existing;

    Images::Image image;
    if (!Images::loadMemory(data, size, &image, error)) return INVALID_TEXTURE;
    if (existing != INVALID_TEXTURE)
        return Internal::publishTexture(existing, std::move(image)) ? existing : INVALID_TEXTURE;
    return store(cache_key, std::move(image));
}

TextureHandle registerTextureImage(
    const std::string& cache_key,
    Images::Image image,
    std::string *error)
{
    if (error) error->clear();
    if (cache_key.empty()) {
        if (error) *error = "empty texture image cache key";
        return INVALID_TEXTURE;
    }

    if (Internal::textureImportActive()) {
        if (const TextureHandle source = deferredAlphaSource(cache_key); source != INVALID_TEXTURE)
            return source;
        const TextureHandle deferred = Internal::registerDeferredDerivedKey(cache_key);
        if (deferred != INVALID_TEXTURE) return deferred;
    }

    if (!validImage(image)) {
        if (error) *error = "invalid texture image";
        return INVALID_TEXTURE;
    }

    const TextureHandle existing = Internal::findTexture(cache_key);
    if (existing != INVALID_TEXTURE && Internal::textureStorageReady(existing)) return existing;
    if (existing != INVALID_TEXTURE)
        return Internal::publishTexture(existing, std::move(image)) ? existing : INVALID_TEXTURE;

    const TextureHandle handle = store(cache_key, std::move(image));
    if (handle == INVALID_TEXTURE && error) *error = "texture handle space exhausted";
    return handle;
}

const TextureAsset *texture(TextureHandle handle)
{
    if (Internal::isStagedTextureHandle(handle)) return Internal::stagedTexture(handle);
    return handle < assets().size() ? &assets()[handle] : nullptr;
}

void clearTextureCache()
{
    Internal::clearTextureStreaming();
    cache().clear();
    assets().clear();
    readiness().clear();
}

} // namespace Models
