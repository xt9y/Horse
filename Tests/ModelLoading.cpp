#include <Core/Jobs/Jobs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Internal/TextureStreaming.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
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
    const Models::TextureAsset *before = Models::texture(handle);
    assert(before != nullptr);
    assert(before->image.rgba.empty());
    assert(Models::Internal::textureState(handle) != Models::Internal::TextureState::Ready);

    Core::Jobs::wait();
    Models::Internal::pumpTextureResources();

    const Models::TextureAsset *after = Models::texture(handle);
    assert(after != nullptr);
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
    const Models::TextureAsset *asset = Models::texture(handle);
    assert(asset != nullptr);
    assert(asset->image.rgba.empty());
    assert(Models::Internal::textureState(handle) != Models::Internal::TextureState::Ready);
}

} // namespace

int main()
{
    testJobsRunWorkOffThreadAndCompletionOnCaller();
    testDeferredTexturePublishesOnlyAfterPump();
    testDeferredTextureDeduplicatesSource();
    testModelImportScopeDoesNotDecodeTextureMemorySynchronously();
    Models::Internal::clearTextureStreaming();
    Models::clearTextureCache();
    Core::Jobs::shutdown();
    return 0;
}
