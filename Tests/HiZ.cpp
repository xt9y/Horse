#include <Renderer/Visibility/HiZ.hpp>

#include <cassert>

int main()
{
    using Renderer::Visibility::HiZ::Extent;
    using Renderer::Visibility::HiZ::mipCount;
    using Renderer::Visibility::HiZ::mipExtent;

    assert(mipCount(0u, 0u) == 0u);
    assert(mipCount(1u, 1u) == 1u);
    assert(mipCount(2560u, 1440u) == 12u);

    assert((mipExtent(5u, 3u, 0u) == Extent{5u, 3u}));
    assert((mipExtent(5u, 3u, 1u) == Extent{2u, 1u}));
    assert((mipExtent(5u, 3u, 2u) == Extent{1u, 1u}));
    assert((mipExtent(5u, 3u, 99u) == Extent{1u, 1u}));
    return 0;
}
