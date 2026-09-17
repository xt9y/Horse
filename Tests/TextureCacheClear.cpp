#include <Core/Jobs/Jobs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Internal/TextureStorage.hpp>
#include <Models/Internal/TextureStreaming.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 70> TinyPng{{
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
    0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xff,0xff,0xff,
    0x7f,0x00,0x09,0xfb,0x03,0xfd,0x2a,0x86,0xe3,0x8a,0x00,0x00,0x00,0x00,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82
}};

} // namespace

int main()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const Models::TextureHandle stale = Models::Internal::registerDeferredMemory(
        "texture-cache-clear.png",
        std::vector<std::uint8_t>{0u, 1u, 2u, 3u}
    );
    assert(stale != Models::INVALID_TEXTURE);

    Models::clearTextureCache();

    const Models::TextureHandle current = Models::Internal::registerDeferredMemory(
        "texture-cache-clear.png",
        std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
    );
    assert(current != Models::INVALID_TEXTURE);

    Core::Jobs::wait();
    Models::Internal::pumpTextureResources();
    assert(Models::Internal::textureState(current) == Models::Internal::TextureState::Ready);
    assert(Models::Internal::textureStorageReady(current));

    Models::clearTextureCache();
    Core::Jobs::shutdown();
    return 0;
}
