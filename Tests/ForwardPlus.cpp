#include <Renderer/Lighting/ForwardPlus.hpp>

#include <cassert>

int main()
{
    using Renderer::Lighting::ForwardPlus::Grid;
    using Renderer::Lighting::ForwardPlus::grid;
    using Renderer::Lighting::ForwardPlus::tileIndex;

    assert((grid(0u, 0u) == Grid{}));
    assert((grid(1u, 1u) == Grid{1u, 1u}));
    assert((grid(32u, 32u) == Grid{1u, 1u}));
    assert((grid(33u, 65u) == Grid{2u, 3u}));
    assert((grid(2560u, 1440u) == Grid{80u, 45u}));

    assert(tileIndex(0u, 0u, Grid{80u, 45u}) == 0u);
    assert(tileIndex(79u, 44u, Grid{80u, 45u}) == 3599u);
    return 0;
}
