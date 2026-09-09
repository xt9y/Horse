#include "Sources/Renderer/Rasterizer/Rasterizer.hpp"
#include "Sources/Renderer/Render.hpp"
#include "Sources/Renderer/Renderer.hpp"

#include <type_traits>

static_assert(std::is_base_of_v<Renderer::IRenderer, Renderer::Rasterizer>);
static_assert(std::is_final_v<Renderer::Rasterizer>);
static_assert(!std::is_copy_constructible_v<Renderer::Rasterizer>);
static_assert(!std::is_copy_assignable_v<Renderer::Rasterizer>);

int main()
{
    Renderer::Rasterizer rasterizer;
    Renderer::IRenderer& renderer = rasterizer;
    renderer.setEnabled(false);
    return renderer.enabled() ? 1 : 0;
}
