#include <Renderer/Features.hpp>
#include <Renderer/Quality.hpp>
#include <Renderer/Reflections/Reflections.hpp>

#include <cassert>
#include <cmath>

int main()
{
    using namespace Renderer;

    Features::Settings& features = Features::settings();
    features = Features::Settings{};
    assert(features.reflections);
    features.reflections = false;
    assert(!Features::currentSettings().reflections);
    features = Features::Settings{};

    Reflections::Settings& settings = Reflections::settings();
    settings = Reflections::Settings{};
    assert(settings.strength == 1.0f);
    assert(Reflections::quality() == Quality::High);

    assert(Reflections::limitEnvironmentMipLevels(13u, Quality::Low) == 5u);
    assert(Reflections::limitEnvironmentMipLevels(13u, Quality::Medium) == 7u);
    assert(Reflections::limitEnvironmentMipLevels(13u, Quality::High) == 9u);
    assert(Reflections::limitEnvironmentMipLevels(13u, Quality::Ultra) == 13u);
    assert(Reflections::limitEnvironmentMipLevels(3u, Quality::Low) == 3u);
    assert(Reflections::limitEnvironmentMipLevels(0u, Quality::Ultra) == 1u);

    assert(Reflections::environmentMipLevels(4096u, 2048u, Quality::Low) == 5u);
    assert(Reflections::environmentMipLevels(4096u, 2048u, Quality::Medium) == 7u);
    assert(Reflections::environmentMipLevels(4096u, 2048u, Quality::High) == 9u);
    assert(Reflections::environmentMipLevels(4096u, 2048u, Quality::Ultra) == 13u);
    assert(Reflections::environmentMipLevels(32u, 16u, Quality::Ultra) == 6u);
    assert(Reflections::environmentMipLevels(1u, 1u, Quality::High) == 1u);

    assert(Reflections::environmentLod(0.0f, 9u) == 0.0f);
    assert(Reflections::environmentLod(1.0f, 9u) == 8.0f);
    assert(std::abs(Reflections::environmentLod(0.5f, 9u) - 4.0f) < 1.0e-6f);
    assert(Reflections::environmentLod(-1.0f, 9u) == 0.0f);
    assert(Reflections::environmentLod(2.0f, 9u) == 8.0f);
    assert(Reflections::environmentLod(0.5f, 1u) == 0.0f);

    Reflections::setQuality(Quality::Low);
    assert(Reflections::quality() == Quality::Low);
    Reflections::setQuality(Quality::Ultra);
    assert(Reflections::quality() == Quality::Ultra);

    settings.strength = 1.5f;
    assert(Reflections::currentSettings().strength == 1.5f);

    Reflections::setQuality(Quality::High);
    settings = Reflections::Settings{};
    return 0;
}
