#include "Renderer/GaussianSplat/GaussianSplat.hpp"

#include "Renderer/GaussianSplat/OpenGL/GaussianSplatOpenGL.hpp"

namespace Renderer::GaussianSplat {

bool render(const Ecs::World& world, Internal::FrameOutput& output)
{
    if (output.api == Internal::GraphicsApi::OpenGL)
        return OpenGL::render(world, output);
    return true;
}

void shutdown()
{
    OpenGL::shutdown();
}

} // namespace Renderer::GaussianSplat
