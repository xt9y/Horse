#include <Renderer/AmbientOcclusion/AmbientOcclusion.hpp>
#include <Renderer/Features.hpp>

#include <cassert>

int main()
{
    using namespace Renderer;

    Features::Settings& features = Features::settings();
    features = Features::Settings{};
    assert(features.ambient_occlusion);
    features.ambient_occlusion = false;
    assert(!Features::currentSettings().ambient_occlusion);
    features = Features::Settings{};

    AmbientOcclusion::Settings& settings = AmbientOcclusion::settings();
    settings = AmbientOcclusion::Settings{};

    assert(settings.strength == 1.0f);
    assert(settings.radius == 1.0f);
    assert(AmbientOcclusion::quality() == Quality::High);

    AmbientOcclusion::setQuality(Quality::Low);
    assert(AmbientOcclusion::quality() == Quality::Low);
    assert(AmbientOcclusion::currentSettings().sample_count == 4u);

    AmbientOcclusion::setQuality(Quality::Medium);
    assert(AmbientOcclusion::currentSettings().sample_count == 6u);

    AmbientOcclusion::setQuality(Quality::High);
    assert(AmbientOcclusion::currentSettings().sample_count == 8u);

    AmbientOcclusion::setQuality(Quality::Ultra);
    assert(AmbientOcclusion::currentSettings().sample_count == 12u);

    settings.strength = 1.4f;
    settings.radius = 2.25f;
    assert(AmbientOcclusion::currentSettings().strength == 1.4f);
    assert(AmbientOcclusion::currentSettings().radius == 2.25f);

    AmbientOcclusion::setQuality(Quality::High);
    settings = AmbientOcclusion::Settings{};
    return 0;
}
