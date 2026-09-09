#include "Sources/Renderer/Renderer.hpp"
#include "Sources/Renderer/PathTracer/PathTracer.hpp"

#include <type_traits>

static_assert(std::is_abstract_v<Renderer::IRenderer>);
static_assert(std::is_base_of_v<Renderer::IRenderer, Renderer::PathTracer>);
static_assert(std::has_virtual_destructor_v<Renderer::IRenderer>);

int main()
{
    Renderer::PathTracer tracer;
    Renderer::IRenderer& renderer = tracer;
    (void)renderer.enabled();
    return 0;
}
