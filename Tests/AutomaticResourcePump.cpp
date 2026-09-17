#include "Ecs/Ecs.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"
#include "Models/Models.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
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
    Models::clearCache();

    Models::TextureHandle handle = Models::INVALID_TEXTURE;
    {
        Models::Internal::TextureImportScope import;
        handle = Models::loadTextureMemory(
            "automatic-resource-pump.png",
            TinyPng.data(),
            TinyPng.size());
    }

    assert(handle != Models::INVALID_TEXTURE);
    assert(!Models::Internal::textureStorageReady(handle));

    Ecs::World world;
    Renderer::Scenes::SceneCache cache;
    const std::vector<Renderer::Scenes::Scene::RenderItem> items;
    std::string error;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!Models::Internal::textureStorageReady(handle) &&
           std::chrono::steady_clock::now() < deadline)
    {
        assert(cache.syncResources(world, items, 31u, &error));
        assert(error.empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(Models::Internal::textureStorageReady(handle));
    assert(Models::Internal::textureState(handle) == Models::Internal::TextureState::Ready);

    Models::clearCache();
    return 0;
}
