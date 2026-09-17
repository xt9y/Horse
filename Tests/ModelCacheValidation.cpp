#include <Core/Jobs/Jobs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Formats/Registry.hpp>
#include <Models/Internal/ModelCache.hpp>
#include <Models/Internal/TextureStreaming.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr std::array<std::uint8_t, 70> TinyPng{{
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,
    0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xff,0xff,0xff,
    0x7f,0x00,0x09,0xfb,0x03,0xfd,0x2a,0x86,0xe3,0x8a,0x00,0x00,0x00,0x00,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82
}};

std::filesystem::path directoryFor(const char *name)
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    error.clear();
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

void writeBytes(const std::filesystem::path& path, const std::uint8_t *data, std::size_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    if (size != 0u) file.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
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

Models::Formats::Document documentWithTexture(Models::TextureHandle texture)
{
    Models::Formats::Document document;
    Models::Formats::Part part;
    part.mesh.vertices = {
        {{0.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}},
    };
    part.mesh.indices = {0u, 1u, 2u};
    part.material.diffuse_texture = texture;
    part.material.base_color_info.texture = texture;
    document.parts.push_back(std::move(part));
    return document;
}

void writeCache(
    const std::filesystem::path& source,
    const Models::Formats::Document& document)
{
    Models::Internal::ModelCache::Payload payload;
    std::string error;
    assert(Models::Internal::ModelCache::encode(source.string(), document, &payload, &error));
    assert(error.empty());
    Models::Internal::ModelCache::writeAsync(source.string(), std::move(payload));
    Core::Jobs::wait();
}

void testSourceMutationInvalidatesCache()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const std::filesystem::path directory = directoryFor("horse-model-cache-source-validation");
    setCacheRoot(directory / "cache");
    const std::filesystem::path source = directory / "model.gltf";
    writeText(source, "source");

    const Models::Formats::Document document = documentWithTexture(Models::INVALID_TEXTURE);
    writeCache(source, document);

    Models::Formats::Document restored;
    std::string error;
    assert(Models::Internal::ModelCache::load(source.string(), &restored, &error));

    writeText(source, "source-mutated");
    assert(!Models::Internal::ModelCache::load(source.string(), &restored, &error));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testExternalTextureMutationInvalidatesCache()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const std::filesystem::path directory = directoryFor("horse-model-cache-texture-validation");
    setCacheRoot(directory / "cache");
    const std::filesystem::path source = directory / "model.gltf";
    const std::filesystem::path texture_path = directory / "texture.png";
    writeText(source, "source");
    writeBytes(texture_path, TinyPng.data(), TinyPng.size());

    const Models::TextureHandle texture = Models::Internal::registerDeferredFile(texture_path.string());
    assert(texture != Models::INVALID_TEXTURE);
    const Models::Formats::Document document = documentWithTexture(texture);
    writeCache(source, document);

    Models::Formats::Document restored;
    std::string error;
    assert(Models::Internal::ModelCache::load(source.string(), &restored, &error));

    std::vector<std::uint8_t> mutated(TinyPng.begin(), TinyPng.end());
    mutated.push_back(0u);
    writeBytes(texture_path, mutated.data(), mutated.size());
    assert(!Models::Internal::ModelCache::load(source.string(), &restored, &error));

    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void testSchemaMutationInvalidatesCache()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    const std::filesystem::path directory = directoryFor("horse-model-cache-schema-validation");
    setCacheRoot(directory / "cache");
    const std::filesystem::path source = directory / "model.gltf";
    writeText(source, "source");

    const Models::Formats::Document document = documentWithTexture(Models::INVALID_TEXTURE);
    writeCache(source, document);

    const std::filesystem::path cache_path = Models::Internal::ModelCache::pathFor(source.string());
    {
        std::fstream file(cache_path, std::ios::binary | std::ios::in | std::ios::out);
        assert(file);
        file.seekp(8, std::ios::beg);
        const char stale_schema = static_cast<char>(0xff);
        file.write(&stale_schema, 1);
        assert(file.good());
    }

    Models::Formats::Document restored;
    std::string error;
    assert(!Models::Internal::ModelCache::load(source.string(), &restored, &error));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

struct RunModelCacheValidationTests
{
    RunModelCacheValidationTests()
    {
        testSourceMutationInvalidatesCache();
        testExternalTextureMutationInvalidatesCache();
        testSchemaMutationInvalidatesCache();
    }
};

const RunModelCacheValidationTests run_model_cache_validation_tests;

} // namespace
