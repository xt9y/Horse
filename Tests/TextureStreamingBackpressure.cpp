#include <Core/Jobs/Jobs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Internal/TextureStreaming.hpp>

#include <array>
#include <cassert>
#include <cstdint>
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

    std::size_t queued = 0u;
    std::size_t registered = 0u;
    for (const Models::TextureHandle handle : handles) {
        const Models::Internal::TextureState state = Models::Internal::textureState(handle);
        if (state == Models::Internal::TextureState::Queued) ++queued;
        if (state == Models::Internal::TextureState::Registered) ++registered;
    }

    assert(queued <= 8u);
    assert(registered >= 120u);

    for (;;) {
        Core::Jobs::wait();
        Models::Internal::pumpTextureResources();
        std::size_t ready = 0u;
        for (const Models::TextureHandle handle : handles)
            if (Models::Internal::textureState(handle) == Models::Internal::TextureState::Ready) ++ready;
        if (ready == handles.size()) break;
    }

    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();
}

struct RunTextureStreamingBackpressureTests
{
    RunTextureStreamingBackpressureTests()
    {
        testStreamingBoundsDecodedResultsBeforePump();
    }
};

const RunTextureStreamingBackpressureTests run_texture_streaming_backpressure_tests;

} // namespace
