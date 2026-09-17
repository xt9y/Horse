#include <Core/Jobs/Jobs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Formats/Registry.hpp>
#include <Models/Internal/TextureStreaming.hpp>
#include <Models/Models.hpp>

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

bool loadStreamProbe(
    const std::string&,
    Models::Formats::Document *document,
    std::string *error)
{
    if (error) error->clear();
    if (!document) return false;
    Models::NodeData node;
    node.name = "stream-probe";
    document->nodes.push_back(std::move(node));
    return true;
}

std::size_t countState(
    const std::vector<Models::TextureHandle>& handles,
    Models::Internal::TextureState state)
{
    std::size_t count = 0u;
    for (const Models::TextureHandle handle : handles)
        if (Models::Internal::textureState(handle) == state) ++count;
    return count;
}

void testStreamingBoundsDecodedResultsBeforePump()
{
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();

    std::vector<Models::TextureHandle> handles;
    handles.reserve(128u);
    for (std::size_t index = 0u; index < 128u; ++index) {
        const Models::TextureHandle handle = Models::Internal::registerDeferredMemory(
            "backpressure-" + std::to_string(index) + ".png",
            std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
        );
        assert(handle != Models::INVALID_TEXTURE);
        handles.push_back(handle);
    }

    assert(countState(handles, Models::Internal::TextureState::Queued) <= 8u);
    assert(countState(handles, Models::Internal::TextureState::Registered) >= 120u);

    for (;;) {
        Core::Jobs::wait();
        Models::Internal::pumpTextureResources();
        if (countState(handles, Models::Internal::TextureState::Ready) == handles.size()) break;
    }

    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();
}

void testSequentialModelLoadsAdvanceTextureStreaming()
{
    Models::clearCache();
    assert(Models::Formats::registerLoader(".streamprobe", loadStreamProbe));

    std::vector<Models::TextureHandle> handles;
    handles.reserve(24u);
    for (std::size_t index = 0u; index < 24u; ++index) {
        const Models::TextureHandle handle = Models::Internal::registerDeferredMemory(
            "load-boundary-" + std::to_string(index) + ".png",
            std::vector<std::uint8_t>(TinyPng.begin(), TinyPng.end())
        );
        assert(handle != Models::INVALID_TEXTURE);
        handles.push_back(handle);
    }

    Core::Jobs::wait();
    assert(countState(handles, Models::Internal::TextureState::Ready) == 0u);
    assert(countState(handles, Models::Internal::TextureState::Queued) == 8u);
    assert(countState(handles, Models::Internal::TextureState::Registered) == 16u);

    std::string error;
    const Models::ModelHandle model = Models::load("stream-boundary.streamprobe", &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());
    assert(countState(handles, Models::Internal::TextureState::Ready) >= 8u);
    assert(countState(handles, Models::Internal::TextureState::Registered) <= 8u);

    for (;;) {
        Core::Jobs::wait();
        Models::Internal::pumpTextureResources();
        if (countState(handles, Models::Internal::TextureState::Ready) == handles.size()) break;
    }

    Models::clearCache();
}

struct RunTextureStreamingBackpressureTests
{
    RunTextureStreamingBackpressureTests()
    {
        testStreamingBoundsDecodedResultsBeforePump();
        testSequentialModelLoadsAdvanceTextureStreaming();
    }
};

const RunTextureStreamingBackpressureTests run_texture_streaming_backpressure_tests;

} // namespace
