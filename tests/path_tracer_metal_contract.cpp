#include "Sources/Renderer/PathTracer/PathTracer.hpp"

#include <type_traits>

int main()
{
    static_assert(std::is_default_constructible_v<Renderer::PathTracer>);
    static_assert(!std::is_copy_constructible_v<Renderer::PathTracer>);
#ifdef __APPLE__
    static_assert(sizeof(Renderer::PathTracerSettings) > 0);
#endif
    return 0;
}
