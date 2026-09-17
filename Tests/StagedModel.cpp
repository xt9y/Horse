#include "Models/Core/Texture.hpp"
#include "Models/Internal/StagedModel.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main()
{
    using namespace Models;

    clearTextureCache();
    const std::size_t global_before = Internal::textureStorageCount();

    Internal::StagedModel staged;
    {
        Internal::StagingScope scope(staged);
        Internal::TextureImportScope import;

        const TextureHandle file = loadTexture("Assets/staged.png");
        const std::uint8_t bytes[] = {0x89u, 0x50u, 0x4eu, 0x47u};
        const TextureHandle memory = loadTextureMemory(
            "staged-memory", bytes, sizeof(bytes));
        const TextureHandle channel = Internal::registerDeferredChannel(
            file, 1, "roughness");
        const TextureHandle opacity = Internal::registerDeferredOpacity(
            file, memory);

        assert(file != INVALID_TEXTURE);
        assert(memory != INVALID_TEXTURE);
        assert(channel != INVALID_TEXTURE);
        assert(opacity != INVALID_TEXTURE);
        assert(file != memory);
        assert(Internal::isStagedTextureHandle(file));
        assert(Internal::isStagedTextureHandle(memory));
        assert(Internal::isStagedTextureHandle(channel));
        assert(Internal::isStagedTextureHandle(opacity));
        assert(Internal::textureStorageCount() == global_before);

        Internal::TextureSourceDescriptor descriptor;
        assert(Internal::stagedTextureDescriptor(channel, &descriptor));
        assert(descriptor.kind == Internal::TextureSourceKind::Channel);
        assert(descriptor.source == file);
        assert(descriptor.channel == 1);

        const TextureAsset *asset = texture(file);
        assert(asset);
        assert(!asset->path.empty());
    }

    assert(staged.textures.size() == 4u);
    assert(Internal::textureStorageCount() == global_before);
    clearTextureCache();
    return 0;
}
