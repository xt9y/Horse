#include "Renderer/Lines/Lines.hpp"

#include "Renderer/Internal/DebugRenderPass.hpp"
#include "Renderer/Internal/LinesRenderPass.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <vector>

namespace Renderer::Lines {
namespace {

std::vector<Debug::RenderPass::Vertex>& vertices()
{
    static std::vector<Debug::RenderPass::Vertex> value;
    return value;
}

} // namespace

void clear()
{
    vertices().clear();
}

void reserve(std::size_t segments)
{
    vertices().reserve(segments * 2u);
}

void add(Vec3 from, Vec3 to, Vec4 color)
{
    auto vertex = [&](Vec3 position) {
        return Debug::RenderPass::Vertex{
            {position.x, position.y, position.z, 1.0f},
            {color.x, color.y, color.z, color.w},
        };
    };

    vertices().push_back(vertex(from));
    vertices().push_back(vertex(to));
}

std::size_t count()
{
    return vertices().size() / 2u;
}

namespace Internal {

void render(const Ecs::World& world, Renderer::Internal::FrameOutput& output)
{
    if (vertices().empty()) return;

    const Scenes::Scene::CameraState camera = Scenes::Scene::cameraState(world);
    if (!camera.valid) return;

    Debug::RenderPass::renderSDLGPU(vertices(), camera, output);
}

} // namespace Internal
} // namespace Renderer::Lines
