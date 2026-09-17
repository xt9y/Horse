#ifndef HORSE_MODELS_INTERNAL_TEXTURE_STREAMING_HPP
#define HORSE_MODELS_INTERNAL_TEXTURE_STREAMING_HPP

#include "Models/Core/Texture.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Models {
enum class AlphaMode : std::uint8_t;
}

namespace Models::Internal {

enum class TextureState : std::uint8_t {
    Registered,
    Queued,
    Loading,
    Ready,
    Failed,
};

enum class TextureSourceKind : std::uint8_t {
    File,
    Memory,
    Channel,
    Alpha,
    Opacity,
};

struct TextureSourceDescriptor
{
    TextureSourceKind kind = TextureSourceKind::File;
    std::string key;
    std::vector<std::uint8_t> bytes;
    TextureHandle source = INVALID_TEXTURE;
    TextureHandle secondary = INVALID_TEXTURE;
    int channel = 0;
    AlphaMode alpha_mode;
    float factor = 1.0f;
    float cutoff = 0.5f;
};

TextureHandle registerDeferredFile(const std::string& path);
TextureHandle registerDeferredMemory(const std::string& key, std::vector<std::uint8_t> bytes);
TextureHandle registerDeferredChannel(TextureHandle source, int channel, const std::string& label);
TextureHandle registerDeferredAlpha(TextureHandle source, AlphaMode mode, float factor, float cutoff);
TextureHandle registerDeferredOpacity(TextureHandle color, TextureHandle opacity);

TextureState textureState(TextureHandle handle);
bool textureDescriptor(TextureHandle handle, TextureSourceDescriptor *descriptor);
std::size_t pumpTextureResources();
void clearTextureStreaming();

} // namespace Models::Internal

#endif
