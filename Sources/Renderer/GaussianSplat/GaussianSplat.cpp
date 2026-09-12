#include "Renderer/GaussianSplat/GaussianSplat.hpp"

#include "Renderer/GaussianSplat/OpenGL/GaussianSplatOpenGL.hpp"
#ifdef __APPLE__
#include "Renderer/GaussianSplat/Metal/GaussianSplatMetal.hpp"
#endif

namespace Renderer::GaussianSplat {

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (output.api == Internal::GraphicsApi::OpenGL)
        return OpenGL::render(world, output);
#ifdef __APPLE__
    if (output.api == Internal::GraphicsApi::Metal)
        return Metal::render(world, output);
#endif
    return true;
}

void shutdown()
{
    OpenGL::shutdown();
#ifdef __APPLE__
    Metal::shutdown();
#endif
}

} // namespace Renderer::GaussianSplat
