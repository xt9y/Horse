#include <Camera/Camera.hpp>
#include <Renderer/Environment.hpp>
#include <Renderer/Features.hpp>
#include <Renderer/GaussianSplat/GaussianSplat.hpp>
#include <Renderer/GlobalIllumination/GlobalIllumination.hpp>
#include <Renderer/Lighting/Lighting.hpp>
#include <Renderer/Quality.hpp>
#include <Renderer/Rasterizer/Rasterizer.hpp>
#include <Renderer/Volumetrics/Volumetrics.hpp>

#include <cassert>
#include <cstdint>

int main()
{
    Renderer::Features::Settings& features = Renderer::Features::settings();
    features = Renderer::Features::Settings{};
    assert(features.lighting);
    assert(features.shadows);
    assert(features.environment);
    assert(features.global_illumination);
    assert(features.ambient_occlusion);
    assert(features.reflections);
    assert(features.volumetrics);
    assert(features.gaussian_splat);
    assert(&features == &Renderer::Features::currentSettings());

    Ecs::World feature_world;
    const Ecs::Entity light = feature_world.createEntity();
    feature_world.add<Renderer::Transform>(light, Renderer::Transform{});
    feature_world.add<Renderer::LightComponent>(light, Renderer::LightComponent{});
    feature_world.add<Renderer::ShadowComponent>(light, Renderer::ShadowComponent{});
    const Ecs::Entity environment = feature_world.createEntity();
    feature_world.add<Renderer::EnvironmentComponent>(
        environment,
        Renderer::EnvironmentComponent{}
    );

    Renderer::Lighting::State lighting = Renderer::Lighting::state(feature_world);
    assert(lighting.lights.size() == 1u);
    assert(lighting.lights[0].shadows);
    assert(Renderer::environmentState(feature_world).valid);

    features.shadows = false;
    lighting = Renderer::Lighting::state(feature_world);
    assert(lighting.lights.size() == 1u);
    assert(!lighting.lights[0].shadows);
    assert(feature_world.get<Renderer::ShadowComponent>(light)->enabled);

    features.lighting = false;
    lighting = Renderer::Lighting::state(feature_world);
    assert(lighting.lights.empty());
    assert(feature_world.get<Renderer::LightComponent>(light));

    features.environment = false;
    assert(!Renderer::environmentState(feature_world).valid);
    assert(feature_world.get<Renderer::EnvironmentComponent>(environment)->enabled);
    features = Renderer::Features::Settings{};

    Ecs::World camera_world;
    const Ecs::Entity camera = camera_world.createEntity();
    camera_world.add<Renderer::Transform>(camera, Renderer::Transform{});
    camera_world.add<Camera::CameraComponent>(camera, Camera::CameraComponent{});
    Camera::setEnabled(true);
    assert(Camera::enabled());
    assert(Camera::activeCamera(camera_world) == camera);
    Camera::setEnabled(false);
    assert(!Camera::enabled());
    assert(Camera::activeCamera(camera_world) == Ecs::INVALID_ENTITY);
    assert(camera_world.get<Camera::CameraComponent>(camera)->active);
    Camera::setEnabled(true);

    Renderer::Rasterizer rasterizer;
    assert(rasterizer.depthPrepass());
    assert(rasterizer.hiZ());
    assert(rasterizer.occlusionCulling());
    assert(rasterizer.forwardPlus());
    rasterizer.setDepthPrepass(false);
    rasterizer.setHiZ(false);
    rasterizer.setOcclusionCulling(false);
    rasterizer.setForwardPlus(false);
    assert(!rasterizer.depthPrepass());
    assert(!rasterizer.hiZ());
    assert(!rasterizer.occlusionCulling());
    assert(!rasterizer.forwardPlus());
    rasterizer.setDepthPrepass(true);
    rasterizer.setHiZ(true);
    rasterizer.setOcclusionCulling(true);
    rasterizer.setForwardPlus(true);

    rasterizer.setShadowQuality(Renderer::Quality::Low);
    assert(rasterizer.shadowQuality() == Renderer::Quality::Low);
    assert(rasterizer.shadowResolution() == 256);
    assert(rasterizer.shadowCascades() == 2);
    rasterizer.setShadowQuality(Renderer::Quality::Ultra);
    assert(rasterizer.shadowQuality() == Renderer::Quality::Ultra);
    assert(rasterizer.shadowResolution() == 2048);
    assert(rasterizer.shadowCascades() == 4);

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

    Renderer::GlobalIllumination::setQuality(Renderer::Quality::High);
    assert(Renderer::GlobalIllumination::quality() == Renderer::Quality::High);
    assert(Renderer::GlobalIllumination::currentSettings().rays_per_probe == 64u);
    Renderer::GlobalIllumination::setQuality(Renderer::Quality::Low);
    assert(Renderer::GlobalIllumination::quality() == Renderer::Quality::Low);
    assert(Renderer::GlobalIllumination::currentSettings().rays_per_probe == 16u);
    Renderer::GlobalIllumination::setQuality(Renderer::Quality::High);

    Renderer::Volumetrics::setQuality(Renderer::Quality::High);
    assert(Renderer::Volumetrics::quality() == Renderer::Quality::High);
    assert(Renderer::Volumetrics::currentSettings().resolution_divisor == 2);
    assert(Renderer::Volumetrics::currentSettings().sample_count == 32u);
    Renderer::Volumetrics::setQuality(Renderer::Quality::Ultra);
    assert(Renderer::Volumetrics::quality() == Renderer::Quality::Ultra);
    assert(Renderer::Volumetrics::currentSettings().resolution_divisor == 1);
    Renderer::Volumetrics::setQuality(Renderer::Quality::High);

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

    Renderer::GlobalIllumination::setPaused(false);
    Renderer::GlobalIllumination::setQuality(Renderer::Quality::High);
    features = Renderer::Features::Settings{};
    return 0;
}
