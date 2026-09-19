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

    assert(Reflections::maximumProbes(Quality::Low) == 2u);
    assert(Reflections::maximumProbes(Quality::Medium) == 4u);
    assert(Reflections::maximumProbes(Quality::High) == 8u);
    assert(Reflections::maximumProbes(Quality::Ultra) == 16u);
    assert(Reflections::probeResolution(Quality::Low) == 256u);
    assert(Reflections::probeResolution(Quality::Medium) == 512u);
    assert(Reflections::probeResolution(Quality::High) == 1024u);
    assert(Reflections::probeResolution(Quality::Ultra) == 1024u);

    const Reflections::ProbeAtlasLayout empty_layout =
        Reflections::probeAtlasLayout(0u, false, Quality::High);
    assert(empty_layout.width == 1u);
    assert(empty_layout.height == 1u);
    assert(empty_layout.probe_count == 0u);
    assert(empty_layout.layers == 1u);
    assert(empty_layout.mip_levels == 1u);

    const Reflections::ProbeAtlasLayout environment_layout =
        Reflections::probeAtlasLayout(0u, true, Quality::High);
    assert(environment_layout.width == 1024u);
    assert(environment_layout.height == 512u);
    assert(environment_layout.probe_count == 0u);
    assert(environment_layout.layers == 1u);
    assert(environment_layout.mip_levels == 11u);

    const Reflections::ProbeAtlasLayout low_layout =
        Reflections::probeAtlasLayout(10u, false, Quality::Low);
    assert(low_layout.width == 256u);
    assert(low_layout.height == 128u);
    assert(low_layout.probe_count == 2u);
    assert(low_layout.layers == 3u);
    assert(low_layout.mip_levels == 9u);

    const Reflections::ProbeAtlasLayout high_layout =
        Reflections::probeAtlasLayout(3u, true, Quality::High);
    assert(high_layout.width == 1024u);
    assert(high_layout.height == 512u);
    assert(high_layout.probe_count == 3u);
    assert(high_layout.layers == 4u);
    assert(high_layout.mip_levels == 11u);

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
