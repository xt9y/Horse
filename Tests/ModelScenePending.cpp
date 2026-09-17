#include <Core/Jobs/Jobs.hpp>
#include <Ecs/Ecs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Formats/Registry.hpp>
#include <Models/Internal/TextureStorage.hpp>
#include <Models/Internal/TextureStreaming.hpp>
#include <Models/Models.hpp>
#include <Renderer/ModelScene.hpp>
#include <Renderer/Scenes/SceneCache.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
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

bool loadPendingScene(
    const std::string&,
    Models::Formats::Document *document,
    std::string *error)
{
    if (error) error->clear();
    if (!document) return false;

    const Models::TextureHandle texture = Models::loadTextureMemory(
        "pending-scene.png",
        TinyPng.data(),
        TinyPng.size(),
        error
    );
    if (texture == Models::INVALID_TEXTURE) return false;

    Models::Formats::Part part;
    part.node = 0u;
    part.mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}},
    };
    part.mesh.indices = {0u, 1u, 2u};
    part.mesh.bounds.minimum = {0.0f, 0.0f, 0.0f};
    part.mesh.bounds.maximum = {1.0f, 1.0f, 0.0f};
    part.material.diffuse_texture = texture;
    part.material.base_color_info.texture = texture;
    document->parts.push_back(std::move(part));

    Models::NodeData node;
    node.name = "pending-node";
    node.parts = {0u};
    document->nodes.push_back(std::move(node));

    Models::SceneData scene;
    scene.name = "pending-scene";
    scene.nodes = {0u};
    document->scenes.push_back(std::move(scene));
    document->default_scene = 0u;
    return true;
}

} // namespace

int main()
{
    Models::clearCache();
    assert(Models::Formats::registerLoader(".pendingscene", loadPendingScene));

    std::string error;
    const Models::ModelHandle model = Models::load("pending-model.pendingscene", &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(model) == 1u);

    const Models::ModelPart *part = Models::part(model, 0u);
    assert(part != nullptr);
    const Models::MaterialData *material = Models::material(part->material);
    assert(material != nullptr);
    const Models::TextureHandle texture = material->diffuse_texture;
    assert(texture != Models::INVALID_TEXTURE);
    assert(!Models::Internal::textureStorageReady(texture));
    assert(Models::Internal::textureState(texture) != Models::Internal::TextureState::Ready);

    Ecs::World world;
    Renderer::ModelScene::Instance instance;
    assert(Renderer::ModelScene::instantiate(world, model, &instance, {}, &error));
    assert(error.empty());
    assert(instance.model == model);
    assert(!instance.nodes.empty());

    Renderer::Scenes::SceneCache scene;
    assert(scene.syncResources(world, 31u, &error));
    assert(error.empty());

    Renderer::ModelScene::destroy(world, instance);
    Models::clearCache();
    Core::Jobs::shutdown();
    return 0;
}
