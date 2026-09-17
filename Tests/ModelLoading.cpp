#include <Core/Jobs/Jobs.hpp>
#include <Ecs/Ecs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Formats/GltfDependencies.hpp>
#include <Models/Formats/Registry.hpp>
#include <Models/Internal/ModelCache.hpp>
#include <Models/Internal/TextureStorage.hpp>
#include <Models/Internal/TextureStreaming.hpp>
#include <Renderer/Scenes/SceneCache.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 67> TinyPng{{
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
    0x89,0x00,0x00,0x00,0x0a,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x00,0x00,0x00,
    0x02,0x00,0x01,0xe5,0x27,0xd4,0xa2,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,
    0x42,0x60,0x82
}};

std::filesystem::path temporaryDirectory(const char *name)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    assert(!error);
    return directory;
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(file.good());
}

void setCacheRoot(const std::filesystem::path& path)
{
#ifdef _WIN32
    assert(_putenv_s("HORSE_MODEL_CACHE_DIR", path.string().c_str()) == 0);
#else
    assert(setenv("HORSE_MODEL_CACHE_DIR", path.string().c_str(), 1) == 0);
#endif
}

void testJobsRunWorkOffThreadAndCompletionOnCaller()
{
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id worker{};
    std::thread::id completion{};
    std::atomic<int> value{0};

    const bool submitted = Core::Jobs::trySubmit(
        [&] {
            worker = std::this_thread::get_id();
            value.store(1, std::memory_order_release);
        },
        [&] {
            completion = std::this_thread::get_id();
            assert(value.load(std::memory_order_acquire) == 1);
            value.store(2, std::memory_order_release);
        }
    );

    assert(submitted);
    Core::Jobs::wait();
    assert(value.load(std::memory_order_acquire) == 1);
    assert(Core::Jobs::pump() == 1u);
    assert(value.load(std::memory_order_acquire) == 2);
    assert(worker != caller);
    assert(completion == caller);
}

void testDeferredTexturePublishesOnlyAfterPump()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const auto handle = Models::Internal::registerDeferredMemory(
        "model-loading-test.png",
        std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
    );
    assert(handle != Models::INVALID_TEXTURE);
    assert(Models::texture(handle) != nullptr);
    assert(!Models::Internal::textureStorageReady(handle));
    assert(Models::Internal::textureState(handle) != Models::Internal::TextureState::Ready);

    Core::Jobs::wait();
    Models::Internal::pumpTextureResources();

    const Models::TextureAsset *after = Models::texture(handle);
    assert(after != nullptr);
    assert(Models::Internal::textureStorageReady(handle));
    assert(Models::Internal::textureState(handle) == Models::Internal::TextureState::Ready);
    assert(after->image.width == 1);
    assert(after->image.height == 1);
    assert(after->image.rgba.size() == 4u);
}

void testDeferredTextureDeduplicatesSource()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();
    const auto first = Models::Internal::registerDeferredFile("missing-texture.png");
    const auto second = Models::Internal::registerDeferredFile("./missing-texture.png");
    assert(first == second);
}

void testModelImportScopeDoesNotDecodeTextureMemorySynchronously()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    Models::TextureHandle handle = Models::INVALID_TEXTURE;
    {
        Models::Internal::TextureImportScope import;
        handle = Models::loadTextureMemory(
            "scoped-model-texture.png",
            TinyPng.data(),
            TinyPng.size()
        );
    }

    assert(handle != Models::INVALID_TEXTURE);
    assert(!Models::Internal::textureStorageReady(handle));
    assert(Models::Internal::textureState(handle) != Models::Internal::TextureState::Ready);
}

void testSceneResourceSyncPumpsCompletedTextures()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const Models::TextureHandle handle = Models::Internal::registerDeferredMemory(
        "scene-pump-texture.png",
        std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
    );
    assert(handle != Models::INVALID_TEXTURE);
    Core::Jobs::wait();
    assert(Models::Internal::textureState(handle) != Models::Internal::TextureState::Ready);

    Ecs::World world;
    Renderer::Scenes::SceneCache cache;
    std::string error;
    assert(cache.syncResources(world, 31u, &error));
    assert(error.empty());
    assert(Models::Internal::textureState(handle) == Models::Internal::TextureState::Ready);
}

void testGltfDependenciesRecordExternalBuffers()
{
    const std::filesystem::path directory = temporaryDirectory("horse-model-loading-dependencies");
    const std::filesystem::path gltf = directory / "model.gltf";
    const std::filesystem::path bin = directory / "mesh.bin";
    writeText(bin, "abcd");
    writeText(
        gltf,
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":4}]})"
    );

    Models::Formats::Document document;
    std::string error;
    assert(Models::Formats::GltfDependencies::apply(gltf.string(), &document, &error));
    assert(error.empty());
    assert(document.dependencies.size() == 1u);
    assert(std::filesystem::path(document.dependencies.front()).lexically_normal() ==
        std::filesystem::absolute(bin).lexically_normal());

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

Models::Formats::Document cacheDocument(Models::TextureHandle texture)
{
    Models::Formats::Document document;
    Models::Formats::Part part;
    part.mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}},
    };
    part.mesh.indices = {0u, 1u, 2u};
    part.mesh.bounds.minimum = {0.0f, 0.0f, 0.0f};
    part.mesh.bounds.maximum = {1.0f, 1.0f, 0.0f};
    part.material.name = "cache-material";
    part.material.color = {0.25f, 0.5f, 0.75f};
    part.material.diffuse_texture = texture;
    part.material.base_color_info.texture = texture;
    document.parts.push_back(std::move(part));

    Models::NodeData node;
    node.name = "cache-node";
    node.parts = {0u};
    document.nodes.push_back(std::move(node));

    Models::SceneData scene;
    scene.name = "cache-scene";
    scene.nodes = {0u};
    document.scenes.push_back(std::move(scene));
    document.default_scene = 0u;
    return document;
}

void testCompiledCacheRoundTripAndTextureGraph()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const std::filesystem::path directory = temporaryDirectory("horse-model-cache-roundtrip");
    setCacheRoot(directory / "cache");
    const std::filesystem::path source = directory / "source.gltf";
    writeText(source, "source");

    const Models::TextureHandle texture = Models::Internal::registerDeferredMemory(
        "cache-embedded-image",
        std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
    );
    assert(texture != Models::INVALID_TEXTURE);
    Models::Formats::Document document = cacheDocument(texture);

    Models::Internal::ModelCache::Payload payload;
    std::string error;
    assert(Models::Internal::ModelCache::encode(source.string(), document, &payload, &error));
    assert(error.empty());
    assert(!payload.bytes.empty());
    Models::Internal::ModelCache::writeAsync(source.string(), std::move(payload));
    Core::Jobs::wait();

    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    Models::Formats::Document restored;
    assert(Models::Internal::ModelCache::load(source.string(), &restored, &error));
    assert(error.empty());
    assert(restored.parts.size() == 1u);
    assert(restored.parts[0].mesh.vertices.size() == 3u);
    assert(restored.parts[0].mesh.indices == std::vector<std::uint32_t>({0u, 1u, 2u}));
    assert(restored.parts[0].material.name == "cache-material");
    assert(restored.nodes.size() == 1u && restored.nodes[0].name == "cache-node");
    assert(restored.scenes.size() == 1u && restored.scenes[0].name == "cache-scene");
    assert(restored.default_scene == 0u);

    const Models::TextureHandle restored_texture = restored.parts[0].material.diffuse_texture;
    assert(restored_texture != Models::INVALID_TEXTURE);
    Models::Internal::TextureSourceDescriptor descriptor;
    assert(Models::Internal::textureDescriptor(restored_texture, &descriptor));
    assert(descriptor.kind == Models::Internal::TextureSourceKind::Memory);
    assert(descriptor.bytes.size() == TinyPng.size());

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testCompiledCacheInvalidatesDependenciesAndCorruption()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const std::filesystem::path directory = temporaryDirectory("horse-model-cache-invalidation");
    setCacheRoot(directory / "cache");
    const std::filesystem::path source = directory / "source.gltf";
    const std::filesystem::path bin = directory / "mesh.bin";
    writeText(source, "source");
    writeText(bin, "mesh");

    Models::Formats::Document document = cacheDocument(Models::INVALID_TEXTURE);
    document.dependencies.push_back(bin.string());

    Models::Internal::ModelCache::Payload payload;
    std::string error;
    assert(Models::Internal::ModelCache::encode(source.string(), document, &payload, &error));
    Models::Internal::ModelCache::writeAsync(source.string(), std::move(payload));
    Core::Jobs::wait();

    Models::Formats::Document restored;
    assert(Models::Internal::ModelCache::load(source.string(), &restored, &error));

    writeText(bin, "mesh-mutated");
    assert(!Models::Internal::ModelCache::load(source.string(), &restored, &error));

    writeText(bin, "mesh");
    Models::Internal::ModelCache::Payload fresh;
    assert(Models::Internal::ModelCache::encode(source.string(), document, &fresh, &error));
    Models::Internal::ModelCache::writeAsync(source.string(), std::move(fresh));
    Core::Jobs::wait();

    const std::filesystem::path cache_path = Models::Internal::ModelCache::pathFor(source.string());
    {
        std::fstream file(cache_path, std::ios::binary | std::ios::in | std::ios::out);
        assert(file);
        file.seekg(-1, std::ios::end);
        char byte = 0;
        file.read(&byte, 1);
        byte ^= 0x5a;
        file.seekp(-1, std::ios::end);
        file.write(&byte, 1);
        assert(file.good());
    }
    assert(!Models::Internal::ModelCache::load(source.string(), &restored, &error));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

} // namespace

int main()
{
    testJobsRunWorkOffThreadAndCompletionOnCaller();
    testDeferredTexturePublishesOnlyAfterPump();
    testDeferredTextureDeduplicatesSource();
    testModelImportScopeDoesNotDecodeTextureMemorySynchronously();
    testSceneResourceSyncPumpsCompletedTextures();
    testGltfDependenciesRecordExternalBuffers();
    testCompiledCacheRoundTripAndTextureGraph();
    testCompiledCacheInvalidatesDependenciesAndCorruption();
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();
    Core::Jobs::shutdown();
    return 0;
}
