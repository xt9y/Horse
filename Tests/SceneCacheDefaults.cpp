#include <Renderer/Scenes/SceneCache.hpp>

#include <cassert>

int main()
{
    assert(Renderer::Scenes::SceneCache::leafSize() == 8u);
    return 0;
}
