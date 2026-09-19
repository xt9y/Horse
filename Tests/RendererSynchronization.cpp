#include <Renderer/Renderer.hpp>

#include <cassert>

int main()
{
    assert(!Renderer::waitIdle());
    return 0;
}
