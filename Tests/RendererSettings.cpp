#include <Renderer/GaussianSplat/GaussianSplat.hpp>
#include <Renderer/GlobalIllumination/GlobalIllumination.hpp>
#include <Renderer/SDLGPU/PBRTraceShaders.hpp>

#include <cassert>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace {

bool containsIdentifier(std::string_view source, std::string_view identifier)
{
    std::size_t position = 0u;
    while ((position = source.find(identifier, position)) != std::string_view::npos) {
        const auto identifierCharacter = [](char value) {
            const unsigned char c = static_cast<unsigned char>(value);
            return std::isalnum(c) != 0 || value == '_';
        };
        const bool left = position > 0u && identifierCharacter(source[position - 1u]);
        const std::size_t end = position + identifier.size();
        const bool right = end < source.size() && identifierCharacter(source[end]);
        if (!left && !right) return true;
        position = end;
    }
    return false;
}

} // namespace

int main()
{
    Renderer::GaussianSplat::Settings& splat = Renderer::GaussianSplat::settings();
    assert(splat.enabled);
    assert(splat.radius == 3.0f);
    assert(splat.minimum_depth_epsilon == 0.0025f);
    assert(splat.relative_depth_epsilon == 0.0005f);
    assert(&splat == &Renderer::GaussianSplat::currentSettings());

    splat.enabled = false;
    splat.radius = 2.5f;
    assert(!Renderer::GaussianSplat::currentSettings().enabled);
    assert(Renderer::GaussianSplat::currentSettings().radius == 2.5f);
    splat = Renderer::GaussianSplat::Settings{};

    Renderer::GlobalIllumination::Settings& gi = Renderer::GlobalIllumination::settings();
    assert(gi.rays_per_probe > 0u);
    assert(gi.probe_budget_per_frame > 0u);
    assert(gi.minimum_probe_dimension > 1u);
    assert(gi.maximum_probe_dimension >= gi.minimum_probe_dimension);
    assert(gi.maximum_bounces > 0u);
    assert(&gi == &Renderer::GlobalIllumination::currentSettings());

    Renderer::GlobalIllumination::setRaysPerProbe(96u);
    Renderer::GlobalIllumination::setProbeBudgetPerFrame(7u);
    Renderer::GlobalIllumination::setProbeDimensionRange(5u, 13u);
    Renderer::GlobalIllumination::setBoundsMargin(0.12f, 0.75f);
    Renderer::GlobalIllumination::setRayEpsilon(0.002f);
    Renderer::GlobalIllumination::setMaximumBounces(3u);
    Renderer::GlobalIllumination::setMaximumPhotonCount(12345u);
    Renderer::GlobalIllumination::setPaused(true);

    const Renderer::GlobalIllumination::Settings& current =
        Renderer::GlobalIllumination::currentSettings();
    assert(current.rays_per_probe == 96u);
    assert(current.probe_budget_per_frame == 7u);
    assert(current.minimum_probe_dimension == 5u);
    assert(current.maximum_probe_dimension == 13u);
    assert(current.bounds_margin_scale == 0.12f);
    assert(current.minimum_bounds_margin == 0.75f);
    assert(current.ray_epsilon == 0.002f);
    assert(current.maximum_bounces == 3u);
    assert(current.maximum_photon_count == 12345u);
    assert(current.paused);
    assert(Renderer::GlobalIllumination::paused());

    Ecs::World world;
    const Ecs::Entity gi_entity = world.createEntity();
    world.add<Renderer::GlobalIlluminationComponent>(
        gi_entity,
        Renderer::GlobalIlluminationComponent{.enabled = false}
    );
    assert(!Renderer::GlobalIllumination::enabled(world));
    world.get<Renderer::GlobalIlluminationComponent>(gi_entity)->enabled = true;
    assert(Renderer::GlobalIllumination::enabled(world));

    assert(!containsIdentifier(Renderer::SDLGPU::PBRTraceShaders::Trace, "triangle"));

    Renderer::GlobalIllumination::setPaused(false);
    return 0;
}
