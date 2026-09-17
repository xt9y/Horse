#include <Core/Jobs/Jobs.hpp>
#include <Ecs/Ecs.hpp>
#include <Models/Internal/TextureStreaming.hpp>
#include <Models/Models.hpp>
#include <Renderer/Scenes/SceneCache.hpp>

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
    Models::clearCache();
    const std::uint64_t before = Models::resourceRevision();

    const Models::TextureHandle texture = Models::Internal::registerDeferredMemory(
        "resource-revision-streaming.png",
        std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
    );
    assert(texture != Models::INVALID_TEXTURE);
    assert(Models::resourceRevision() == before);

    Core::Jobs::wait();
    assert(Models::resourceRevision() == before);

    Ecs::World world;
    Renderer::Scenes::SceneCache cache;
    std::string error;
    assert(cache.syncResources(world, 31u, &error));
    assert(error.empty());
    assert(Models::resourceRevision() > before);

    Models::clearCache();
    Core::Jobs::shutdown();
    return 0;
}
