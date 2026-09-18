#include "Renderer/Lines/Lines.hpp"

#include "Camera/Camera.hpp"
#include "Renderer/Internal/DebugRenderPass.hpp"
#include "Renderer/Internal/LinesRenderPass.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <algorithm>
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

    const Vec3 forward = Math::normalize(Camera::flightDirection(
        camera.transform.rotation.y,
        camera.transform.rotation.x
    ));
    const Vec3 right = Math::normalize(Camera::strafeDirection(camera.transform.rotation.y));
    const Vec3 up = Math::normalize(Math::cross(right, forward));

    const float aspect =
        static_cast<float>(std::max(output.width, 1)) /
        static_cast<float>(std::max(output.height, 1));

    const float far_distance = Visibility::system().farDistance();
    if (far_distance <= camera.near_plane) return;

    const Math::Mat4 projection = Math::perspective(
        camera.fov_degrees,
        aspect,
        camera.near_plane,
        far_distance
    );
    const Math::Mat4 view = Math::viewMatrix(
        camera.transform.position,
        forward,
        right,
        up
    );

    Debug::RenderPass::renderSDLGPU(vertices(), projection, view, output);
}

} // namespace Internal
} // namespace Renderer::Lines
