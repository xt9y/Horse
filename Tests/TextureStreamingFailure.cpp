#include <Core/Jobs/Jobs.hpp>
#include <Models/Core/Texture.hpp>
#include <Models/Internal/TextureStreaming.hpp>
#include <Models/Models.hpp>

#include <cassert>
#include <cstdint>

int main()
{
    Models::clearCache();
    const std::uint64_t before = Models::resourceRevision();

    const Models::TextureHandle texture = Models::Internal::registerDeferredFile(
        "this-texture-must-not-exist-horse-streaming-test.png"
    );
    assert(texture != Models::INVALID_TEXTURE);

    Core::Jobs::wait();
    Models::Internal::pumpTextureResources();
    assert(Models::Internal::textureState(texture) == Models::Internal::TextureState::Failed);
    const std::uint64_t failed_revision = Models::resourceRevision();
    assert(failed_revision > before);

    for (int iteration = 0; iteration < 8; ++iteration) {
        Core::Jobs::wait();
        Models::Internal::pumpTextureResources();
        assert(Models::Internal::textureState(texture) == Models::Internal::TextureState::Failed);
        assert(Models::resourceRevision() == failed_revision);
    }

    Models::clearCache();
    Core::Jobs::shutdown();
    return 0;
}
