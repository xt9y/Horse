#include "Models/Models.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void appendF32(std::vector<std::uint8_t>& bytes, float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned int shift = 0u; shift < 32u; shift += 8u)
        bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
}

void writeBytes(const std::filesystem::path& path, const void *data, std::size_t size)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    assert(file);
    if (size != 0u)
        file.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    assert(file.good());
}

std::filesystem::path writeFixture(const std::filesystem::path& directory)
{
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);
    assert(!ec);

    std::vector<std::uint8_t> binary;
    for (float value : std::array<float, 9>{{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f}})
        appendF32(binary, value);
    appendU16(binary, 0u);
    appendU16(binary, 1u);
    appendU16(binary, 2u);
    writeBytes(directory / "mesh.bin", binary.data(), binary.size());
    writeBytes(directory / "white.png", TinyPng.data(), TinyPng.size());

    const std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":42}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
        R"("images":[{"uri":"white.png"}],"textures":[{"source":0}],)"
        R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1.0],"baseColorTexture":{"index":0}}}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
        R"("nodes":[{"name":"Triangle","mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";

    const std::filesystem::path model = directory / "model.gltf";
    writeBytes(model, json.data(), json.size());
    return model;
}

void setCacheRoot(const std::filesystem::path& path)
{
#ifdef _WIN32
    assert(_putenv_s("HORSE_MODEL_CACHE_DIR", path.string().c_str()) == 0);
#else
    assert(setenv("HORSE_MODEL_CACHE_DIR", path.string().c_str(), 1) == 0);
#endif
}

Models::ModelHandle waitFor(Models::LoadHandle request, std::string *error)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const Models::LoadState state = Models::loadState(request);
        if (state == Models::LoadState::Ready) return Models::loadResult(request, error);
        if (state == Models::LoadState::Failed) return Models::loadResult(request, error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (error) *error = "async glTF load timed out";
    return Models::INVALID_MODEL;
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "horse-async-gltf-loading";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    const std::filesystem::path sync_path = writeFixture(root / "sync-asset");
    const std::filesystem::path async_path = writeFixture(root / "async-asset");
    setCacheRoot(root / "cache");

    Models::clearCache();
    std::string error;
    const Models::ModelHandle sync = Models::load(sync_path.string(), &error);
    assert(sync != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(sync) == 1u);
    assert(Models::nodeCount(sync) == 1u);

    const Models::ModelPart *sync_part = Models::part(sync, 0u);
    assert(sync_part);
    const Models::MaterialData *sync_material = Models::material(sync_part->material);
    assert(sync_material);
    const Models::Vec3 expected_color = sync_material->color;
    assert(sync_material->diffuse_texture != Models::INVALID_TEXTURE);

    Models::clearCache();

    const auto begin = std::chrono::steady_clock::now();
    const Models::LoadHandle request = Models::loadAsync(async_path.string());
    const auto submit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - begin);
    assert(request != Models::INVALID_LOAD);
    assert(submit_ms < std::chrono::milliseconds(100));

    const Models::ModelHandle async = waitFor(request, &error);
    assert(async != Models::INVALID_MODEL);
    assert(error.empty());
    assert(Models::partCount(async) == 1u);
    assert(Models::nodeCount(async) == 1u);

    const Models::ModelPart *async_part = Models::part(async, 0u);
    assert(async_part);
    const Models::MaterialData *async_material = Models::material(async_part->material);
    assert(async_material);
    assert(async_material->color.x == expected_color.x);
    assert(async_material->color.y == expected_color.y);
    assert(async_material->color.z == expected_color.z);
    assert(async_material->diffuse_texture != Models::INVALID_TEXTURE);

    Models::clearCache();
    return 0;
}
