#ifndef RW_ENGINE_MODELS_CORE_TEXTURE_HPP
#define RW_ENGINE_MODELS_CORE_TEXTURE_HPP

#include "Models/Images/Image.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Models {

using TextureHandle = std::uint32_t;
constexpr TextureHandle INVALID_TEXTURE = UINT32_MAX;

struct TextureAsset {
    std::string path;
    Images::Image image;
};

TextureHandle loadTexture(const std::string& path, std::string *error = nullptr);
TextureHandle loadTextureWithOpacity(
    const std::string& color_path,
    const std::string& opacity_path,
    std::string *error = nullptr
);
TextureHandle combineTextureOpacity(
    TextureHandle color,
    TextureHandle opacity,
    std::string *error = nullptr
);
TextureHandle loadTextureMemory(
    const std::string& cache_key,
    const void *data,
    std::size_t size,
    std::string *error = nullptr
);
TextureHandle registerTextureImage(
    const std::string& cache_key,
    Images::Image image,
    std::string *error = nullptr
);
const TextureAsset *texture(TextureHandle handle);
void clearTextureCache();

} // namespace Models

#endif
