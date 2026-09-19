#include <Renderer/Lighting/ForwardPlus.hpp>

#include <cassert>

int main()
{
    using namespace Renderer::Lighting::ForwardPlus;

    assert((grid(0u, 0u) == Grid{}));
    assert((grid(1u, 1u) == Grid{1u, 1u}));
    assert((grid(32u, 32u) == Grid{1u, 1u}));
    assert((grid(33u, 65u) == Grid{2u, 3u}));
    assert((grid(2560u, 1440u) == Grid{80u, 45u}));

    assert(tileIndex(0u, 0u, Grid{80u, 45u}) == 0u);
    assert(tileIndex(79u, 44u, Grid{80u, 45u}) == 3599u);

    const BufferLayout uhd = bufferLayout(3840u, 2160u);
    assert(uhd.valid);
    assert((uhd.grid == Grid{120u, 68u}));
    assert(uhd.tile_count == 8160u);
    assert(uhd.index_count == 522240u);
    assert(uhd.count_bytes == 32640u);
    assert(uhd.index_bytes == 2088960u);

    const BufferLayout eight_k = bufferLayout(7680u, 4320u);
    assert(eight_k.valid);
    assert((eight_k.grid == Grid{240u, 135u}));
    assert(eight_k.tile_count == 32400u);
    assert(eight_k.index_count == 2073600u);
    assert(eight_k.count_bytes == 129600u);
    assert(eight_k.index_bytes == 8294400u);

    assert(!bufferLayout(0u, 0u).valid);
    assert(!tileUsesFullLightLoop(MaximumLightsPerTile));
    assert(tileUsesFullLightLoop(OverflowCount));

    return 0;
}
