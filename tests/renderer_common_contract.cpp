#include "Sources/Camera.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Math.hpp"
#include "Sources/Renderer/PathTracer/PathTracer.hpp"
#include "Sources/Renderer/Renderer.hpp"
#include "Sources/Renderer/Scene.hpp"

#include <cassert>
#include <cmath>
#include <type_traits>
#include <vector>

static_assert(std::is_abstract_v<Renderer::IRenderer>);
static_assert(std::is_base_of_v<Renderer::IRenderer, Renderer::PathTracer>);
static_assert(std::has_virtual_destructor_v<Renderer::IRenderer>);

namespace {

bool near(float a, float b)
{
    return std::fabs(a - b) < 1.0e-4f;
}

} // namespace

int main()
{
    Renderer::PathTracer tracer;
    Renderer::IRenderer& renderer = tracer;
    (void)renderer.enabled();

    Ecs::World world;

    const Ecs::Entity camera = world.createEntity();
    world.add<Renderer::Transform>(camera, Renderer::Transform{
        .position = {1.0f, 2.0f, 3.0f},
        .rotation = {10.0f, 20.0f, 30.0f},
        .scale = {1.0f, 1.0f, 1.0f},
    });
    world.add<Camera::CameraComponent>(camera, Camera::CameraComponent{
        .fov_degrees = 75.0f,
        .near_plane = 0.25f,
        .active = true,
    });

    const Ecs::Entity light = world.createEntity();
    world.add<Renderer::Transform>(light, Renderer::Transform{
        .position = {4.0f, 5.0f, 6.0f},
        .rotation = {},
        .scale = {1.0f, 1.0f, 1.0f},
    });
    world.add<Renderer::LightComponent>(light, Renderer::LightComponent{
        .type = Renderer::LightType::Point,
        .color = {0.5f, 0.6f, 0.7f},
        .intensity = 3.0f,
    });

    Models::MeshData mesh;
    mesh.vertices = {
        Models::Vertex{.position = {0.0f, 0.0f, 0.0f}},
        Models::Vertex{.position = {1.0f, 0.0f, 0.0f}},
        Models::Vertex{.position = {0.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.bounds.minimum = {0.0f, 0.0f, 0.0f};
    mesh.bounds.maximum = {1.0f, 1.0f, 0.0f};

    Models::MaterialData material;
    material.name = "renderer common contract";
    material.color = {0.2f, 0.4f, 0.6f};

    const Models::MeshHandle mesh_handle = Models::registerMesh(std::move(mesh));
    const Models::MaterialHandle material_handle = Models::registerMaterial(std::move(material));
    assert(mesh_handle != Models::INVALID_MESH);
    assert(material_handle != Models::INVALID_MATERIAL);

    const Ecs::Entity visible = world.createEntity();
    world.add<Renderer::Transform>(visible, Renderer::Transform{});
    world.add<Renderer::MeshComponent>(visible, Renderer::MeshComponent{mesh_handle, material_handle});
    world.add<Renderer::RenderableComponent>(visible, Renderer::RenderableComponent{true});

    const Ecs::Entity no_material = world.createEntity();
    world.add<Renderer::Transform>(no_material, Renderer::Transform{});
    world.add<Renderer::MeshComponent>(no_material, Renderer::MeshComponent{mesh_handle, Models::INVALID_MATERIAL});
    world.add<Renderer::RenderableComponent>(no_material, Renderer::RenderableComponent{true});

    const Ecs::Entity hidden = world.createEntity();
    world.add<Renderer::Transform>(hidden, Renderer::Transform{});
    world.add<Renderer::MeshComponent>(hidden, Renderer::MeshComponent{mesh_handle, material_handle});
    world.add<Renderer::RenderableComponent>(hidden, Renderer::RenderableComponent{false});

    const Ecs::Entity invalid_mesh = world.createEntity();
    world.add<Renderer::Transform>(invalid_mesh, Renderer::Transform{});
    world.add<Renderer::MeshComponent>(invalid_mesh, Renderer::MeshComponent{Models::INVALID_MESH, material_handle});
    world.add<Renderer::RenderableComponent>(invalid_mesh, Renderer::RenderableComponent{true});

    const Renderer::Scene::CameraState camera_state = Renderer::Scene::cameraState(world);
    assert(camera_state.valid);
    assert(camera_state.entity == camera);
    assert(near(camera_state.fov_degrees, 75.0f));
    assert(near(camera_state.near_plane, 0.25f));
    assert(near(camera_state.transform.position.x, 1.0f));

    const Renderer::Scene::LightState light_state = Renderer::Scene::lightState(world);
    assert(light_state.valid);
    assert(light_state.entity == light);
    assert(light_state.light.type == Renderer::LightType::Point);
    assert(near(light_state.light.intensity, 3.0f));
    assert(near(light_state.transform.position.z, 6.0f));

    std::vector<Renderer::Scene::RenderItem> items;
    Renderer::Scene::collectRenderItems(world, items);
    assert(items.size() == 2u);
    assert(items[0].entity == visible);
    assert(items[0].mesh != nullptr);
    assert(items[0].material != nullptr);
    assert(items[1].entity == no_material);
    assert(items[1].mesh != nullptr);
    assert(items[1].material == nullptr);

    const Renderer::Transform transform{
        .position = {1.0f, 2.0f, 3.0f},
        .rotation = {0.0f, 0.0f, 90.0f},
        .scale = {2.0f, 1.0f, 1.0f},
    };
    const Renderer::Math::Mat4 model = Renderer::Math::modelMatrix(transform);
    const Renderer::Vec3 transformed = Renderer::Math::transformPoint(model, {1.0f, 0.0f, 0.0f});
    assert(near(transformed.x, 1.0f));
    assert(near(transformed.y, 4.0f));
    assert(near(transformed.z, 3.0f));

    const Renderer::Math::Mat4 inverse = Renderer::Math::inverseModelMatrix(transform);
    const Renderer::Vec3 restored = Renderer::Math::transformPoint(inverse, transformed);
    assert(near(restored.x, 1.0f));
    assert(near(restored.y, 0.0f));
    assert(near(restored.z, 0.0f));

    Models::clearCache();
    return 0;
}
