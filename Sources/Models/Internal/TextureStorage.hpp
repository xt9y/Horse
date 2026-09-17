#ifndef HORSE_MODELS_INTERNAL_TEXTURE_STORAGE_HPP
#define HORSE_MODELS_INTERNAL_TEXTURE_STORAGE_HPP

#include "Models/Core/Texture.hpp"

#include <cstddef>
#include <string>

namespace Models::Internal {

std::string normalizeTexturePath(const std::string& path);
TextureHandle findTexture(const std::string& key);
TextureHandle reserveTexture(const std::string& key);
std::size_t textureStorageCount();
bool textureStorageReady(TextureHandle handle);
bool publishTexture(TextureHandle handle, Images::Image image);

} // namespace Models::Internal

#endif
