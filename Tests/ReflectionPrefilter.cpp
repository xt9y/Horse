#include <Renderer/Internal/ReflectionPrefilter.hpp>
#include <Renderer/Quality.hpp>

#include <Models/Images/Image.hpp>

#include <cassert>
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

    return 0;
}
