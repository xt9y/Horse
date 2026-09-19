#include <Renderer/Internal/ReflectionPrefilter.hpp>
#include <Renderer/Quality.hpp>

#include <Models/Images/Image.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

int main()
{
    using namespace Renderer;
    using namespace Renderer::Reflections::Internal;

    assert(prefilterSampleCount(Quality::Low) == 8u);
    assert(prefilterSampleCount(Quality::Medium) == 16u);
    assert(prefilterSampleCount(Quality::High) == 32u);
    assert(prefilterSampleCount(Quality::Ultra) == 64u);

    Models::Images::Image source;
    source.width = 4;
    source.height = 2;
    source.rgba.resize(4u * 2u * 4u);
    for (std::size_t pixel = 0u; pixel < 8u; ++pixel) {
        source.rgba[pixel * 4u + 0u] = 128u;
        source.rgba[pixel * 4u + 1u] = 64u;
        source.rgba[pixel * 4u + 2u] = 32u;
        source.rgba[pixel * 4u + 3u] = 255u;
    }

    const PrefilterChain chain = prefilterEquirectangular(
        source,
        4u,
        2u,
        3u,
        Quality::High
    );

    assert(chain.levels.size() == 3u);
    assert(chain.levels[0].width == 4u && chain.levels[0].height == 2u);
    assert(chain.levels[1].width == 2u && chain.levels[1].height == 1u);
    assert(chain.levels[2].width == 1u && chain.levels[2].height == 1u);

    for (const PrefilterLevel& level : chain.levels) {
        assert(level.rgba.size() ==
            static_cast<std::size_t>(level.width) * level.height * 4u);
        for (std::size_t pixel = 0u; pixel < level.rgba.size(); pixel += 4u) {
            assert(std::abs(static_cast<int>(level.rgba[pixel + 0u]) - 128) <= 1);
            assert(std::abs(static_cast<int>(level.rgba[pixel + 1u]) - 64) <= 1);
            assert(std::abs(static_cast<int>(level.rgba[pixel + 2u]) - 32) <= 1);
            assert(level.rgba[pixel + 3u] == 255u);
        }
    }

    const IrradianceSH constant_irradiance = irradianceEquirectangular(source);
    assert(std::abs(constant_irradiance.average[0] - 0.215861f) < 1.0e-4f);
    assert(std::abs(constant_irradiance.average[1] - 0.051269f) < 1.0e-4f);
    assert(std::abs(constant_irradiance.average[2] - 0.014444f) < 1.0e-4f);
    for (std::size_t channel = 0u; channel < 3u; ++channel) {
        assert(std::abs(constant_irradiance.x[channel]) < 1.0e-5f);
        assert(std::abs(constant_irradiance.y[channel]) < 1.0e-5f);
        assert(std::abs(constant_irradiance.z[channel]) < 1.0e-5f);
    }

    Models::Images::Image directional;
    directional.width = 4;
    directional.height = 2;
    directional.rgba.resize(4u * 2u * 4u, 0u);
    for (std::size_t pixel = 0u; pixel < 8u; ++pixel)
        directional.rgba[pixel * 4u + 3u] = 255u;
    for (int y = 0; y < directional.height; ++y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * 4u + 2u) * 4u;
        directional.rgba[offset + 0u] = 255u;
        directional.rgba[offset + 1u] = 255u;
        directional.rgba[offset + 2u] = 255u;
    }
    const IrradianceSH directional_irradiance = irradianceEquirectangular(directional);
    assert(directional_irradiance.x[0] > 0.0f);
    assert(directional_irradiance.z[0] > 0.0f);
    assert(std::abs(directional_irradiance.y[0]) < 1.0e-5f);

    const Models::Images::Image empty;
    const PrefilterChain fallback = prefilterEquirectangular(
        empty,
        1u,
        1u,
        1u,
        Quality::Low
    );
    assert(fallback.levels.size() == 1u);
    assert(fallback.levels[0].rgba.size() == 4u);
    assert(fallback.levels[0].rgba[0] == 0u);
    assert(fallback.levels[0].rgba[1] == 0u);
    assert(fallback.levels[0].rgba[2] == 0u);
    assert(fallback.levels[0].rgba[3] == 255u);

    const IrradianceSH empty_irradiance = irradianceEquirectangular(empty);
    for (float value : empty_irradiance.average) assert(value == 0.0f);
    for (float value : empty_irradiance.x) assert(value == 0.0f);
    for (float value : empty_irradiance.y) assert(value == 0.0f);
    for (float value : empty_irradiance.z) assert(value == 0.0f);

    return 0;
}
