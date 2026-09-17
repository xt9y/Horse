#include "Core/Jobs/Jobs.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Internal/ModelCache.hpp"
#include "Models/Internal/StagedModel.hpp"
#include "Models/Internal/TextureStorage.hpp"
#include "Models/Internal/TextureStreaming.hpp"
#include "Models/Models.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void setCacheRoot(const std::filesystem::path& path)
{
#ifdef _WIN32
    assert(_putenv_s("HORSE_MODEL_CACHE_DIR", path.string().c_str()) == 0);
#else
    assert(setenv("HORSE_MODEL_CACHE_DIR", path.string().c_str(), 1) == 0);
#endif
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "horse-async-model-cache";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    assert(!ec);
    setCacheRoot(root / "cache");

    const std::filesystem::path source = root / "source.gltf";
    {
        std::ofstream file(source, std::ios::binary | std::ios::trunc);
        file << "cache source";
        assert(file.good());
    }

    Models::clearCache();
    assert(Models::Internal::textureStorageCount() == 0u);

    Models::Internal::StagedModel encoded;
    {
        Models::Internal::StagingScope staging(encoded);
        Models::Internal::TextureImportScope import;

        const Models::TextureHandle texture = Models::loadTextureMemory(
            source.string() + "#image:0",
            TinyPng.data(),
            TinyPng.size());
        assert(texture != Models::INVALID_TEXTURE);
        assert(Models::Internal::isStagedTextureHandle(texture));

        Models::Formats::Part part;
        part.mesh.vertices.resize(3u);
        part.mesh.indices = {0u, 1u, 2u};
        part.material.diffuse_texture = texture;
        part.material.base_color_info.texture = texture;
        encoded.document.parts.push_back(std::move(part));

        Models::Internal::ModelCache::Payload payload;
        std::string error;
        assert(Models::Internal::ModelCache::encode(
            source.string(), encoded.document, &payload, &error));
        assert(error.empty());
        Models::Internal::ModelCache::writeAsync(source.string(), std::move(payload));
    }

    assert(Models::Internal::textureStorageCount() == 0u);
    Core::Jobs::wait();

    Models::Internal::StagedModel decoded;
    {
        Models::Internal::StagingScope staging(decoded);
        Models::Internal::TextureImportScope import;
        std::string error;
        assert(Models::Internal::ModelCache::load(source.string(), &decoded.document, &error));
        assert(error.empty());
        assert(Models::Internal::textureStorageCount() == 0u);
        assert(decoded.textures.size() == 1u);
        assert(decoded.document.parts.size() == 1u);
        const Models::TextureHandle texture = decoded.document.parts[0].material.diffuse_texture;
        assert(Models::Internal::isStagedTextureHandle(texture));
    }

    assert(Models::Internal::textureStorageCount() == 0u);
    Models::clearCache();
    std::filesystem::remove_all(root, ec);
    return 0;
}
