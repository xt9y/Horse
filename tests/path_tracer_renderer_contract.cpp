#include "Sources/Renderer/Render.hpp"

#include <type_traits>

int main()
{
    static_assert(std::is_default_constructible_v<Renderer::PathTracer>);
    static_assert(!std::is_copy_constructible_v<Renderer::PathTracer>);
    return 0;
}
