#include "Ecs/Ecs.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Formats/Registry.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"
#include "Models/Models.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 70> TinyPng{{
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
    0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xff,0xff,0xff,
    0x7f,0x00,0x09,0xfb,0x03,0xfd,0x2a,0x86,0xe3,0x8a,0x00,0x00,0x00,0x00,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82
}};

bool automaticModelLoader(
    const std::string&,
    Models::Formats::Document *document,
    std::string *error)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    if (error) error->clear();
    if (!document) return false;

    Models::Formats::Part part;
    part.mesh.vertices.resize(3u);
    part.mesh.indices = {0u, 1u, 2u};
    document->parts.push_back(std::move(part));
    return true;
}

void pumpFrame(
    const Ecs::World& world,
    std::vector<Renderer::Scenes::Scene::RenderItem>& items,
    const std::chrono::steady_clock::time_point deadline)
{
    Renderer::Scenes::Scene::collectRenderItems(world, items);
    if (std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

} // namespace

int main()
{
    using Clock = std::chrono::steady_clock;

    Models::clearCache();

    Models::TextureHandle texture = Models::INVALID_TEXTURE;
    {
        Models::Internal::TextureImportScope import;
        texture = Models::loadTextureMemory(
            "automatic-resource-pump.png",
            TinyPng.data(),
            TinyPng.size());
    }

    assert(texture != Models::INVALID_TEXTURE);
    assert(!Models::Internal::textureStorageReady(texture));

    Ecs::World world;
    std::vector<Renderer::Scenes::Scene::RenderItem> items;
    const auto texture_deadline = Clock::now() + std::chrono::seconds(2);
    while (!Models::Internal::textureStorageReady(texture) &&
           Clock::now() < texture_deadline)
        pumpFrame(world, items, texture_deadline);

    assert(Models::Internal::textureStorageReady(texture));
    assert(Models::Internal::textureState(texture) == Models::Internal::TextureState::Ready);

    Models::clearCache();
    assert(Models::Formats::registerLoader(".autopump", automaticModelLoader));

    const std::uint64_t revision_before = Models::resourceRevision();
    const Models::LoadHandle request = Models::loadAsync("automatic-model.autopump");
    assert(request != Models::INVALID_LOAD);

    const auto model_deadline = Clock::now() + std::chrono::seconds(2);
    while (Models::resourceRevision() == revision_before &&
           Clock::now() < model_deadline)
        pumpFrame(world, items, model_deadline);

    assert(Models::resourceRevision() > revision_before);

    std::string error;
    const Models::ModelHandle model = Models::loadResult(request, &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(model) == 1u);

    Models::clearCache();
    return 0;
}
