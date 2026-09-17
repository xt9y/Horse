#include <Core/Jobs/Jobs.hpp>
#include <Models/Internal/TextureStorage.hpp>
#include <Models/Models.hpp>

#include <array>
#include <bit>
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

void setCacheRoot(const std::filesystem::path& path)
{
#ifdef _WIN32
    assert(_putenv_s("HORSE_MODEL_CACHE_DIR", path.string().c_str()) == 0);
#else
    assert(setenv("HORSE_MODEL_CACHE_DIR", path.string().c_str(), 1) == 0);
#endif
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(file.good());
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(file.good());
}

void appendU16(std::vector<std::uint8_t> *bytes, std::uint16_t value)
{
    bytes->push_back(static_cast<std::uint8_t>(value));
    bytes->push_back(static_cast<std::uint8_t>(value >> 8u));
}

void appendF32(std::vector<std::uint8_t> *bytes, float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned int shift = 0u; shift < 32u; shift += 8u)
        bytes->push_back(static_cast<std::uint8_t>(bits >> shift));
}

std::filesystem::path writeMaskedModel(const std::filesystem::path& directory)
{
    std::vector<std::uint8_t> binary;
    for (float value : std::array<float, 9>{{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f}})
        appendF32(&binary, value);
    appendU16(&binary, 0u);
    appendU16(&binary, 1u);
    appendU16(&binary, 2u);
    writeBytes(directory / "mesh.bin", binary);
    writeBytes(directory / "white.png", std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end()));

    const std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":42}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
        R"("images":[{"uri":"white.png"}],"textures":[{"source":0}],)"
        R"("materials":[{"alphaMode":"MASK","alphaCutoff":0.5,"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
        R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";

    const std::filesystem::path path = directory / "masked.gltf";
    writeText(path, json);
    return path;
}

} // namespace

int main()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "horse-gltf-texture-derivation";
    std::error_code error_code;
    std::filesystem::remove_all(directory, error_code);
    error_code.clear();
    std::filesystem::create_directories(directory, error_code);
    assert(!error_code);
    setCacheRoot(directory / "cache");

    Models::clearCache();
    std::string error;
    const Models::ModelHandle model = Models::load(writeMaskedModel(directory).string(), &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::Internal::textureStorageCount() == 2u);

    Models::clearCache();
    Core::Jobs::shutdown();
    std::filesystem::remove_all(directory, error_code);
    return 0;
}
