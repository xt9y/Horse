#include "Sources/Camera.hpp"
#include "Sources/Models/Models.hpp"
#include "Sources/Renderer/Render.hpp"

#include <lwcgl/lwcgl.h>

#include <cassert>

int main()
{
    lwcglInstallFastRuntime();
    Display.setDisplayMode(new DisplayMode(96, 64));
    Display.create();

    Renderer::Rasterizer rasterizer;
    assert(!rasterizer.initialized());
    assert(rasterizer.enabled());
    assert(rasterizer.init());
    assert(rasterizer.initialized());
    rasterizer.resize(96, 64);

    Ecs::World world;
    const Ecs::Entity camera = world.createEntity();
    world.add<Renderer::Transform>(camera, Renderer::Transform{
        .position = {0.0f, 0.0f, 3.0f},
        .rotation = {},
        .scale = {1.0f, 1.0f, 1.0f},
    });
    world.add<Camera::CameraComponent>(camera, Camera::CameraComponent{});

    Models::MeshData mesh;
    mesh.vertices.resize(3u);
    mesh.vertices[0].position = {-0.5f, -0.5f, 0.0f};
    mesh.vertices[1].position = { 0.5f, -0.5f, 0.0f};
    mesh.vertices[2].position = { 0.0f,  0.5f, 0.0f};
    for (auto& vertex : mesh.vertices) vertex.normal = {0.0f, 0.0f, 1.0f};
    mesh.indices = {0u, 1u, 2u};
    mesh.bounds.minimum = {-0.5f, -0.5f, 0.0f};
    mesh.bounds.maximum = { 0.5f,  0.5f, 0.0f};

    Models::MaterialData material;
    material.color = {0.8f, 0.3f, 0.2f};
    const auto mesh_handle = Models::registerMesh(std::move(mesh));
    const auto material_handle = Models::registerMaterial(std::move(material));

    const Ecs::Entity object = world.createEntity();
    world.add<Renderer::Transform>(object, Renderer::Transform{});
    world.add<Renderer::MeshComponent>(object, Renderer::MeshComponent{mesh_handle, material_handle});
    world.add<Renderer::RenderableComponent>(object, Renderer::RenderableComponent{true});

    rasterizer.render(world);
    rasterizer.setEnabled(false);
    assert(!rasterizer.enabled());
    rasterizer.render(world);
    rasterizer.setEnabled(true);
    assert(rasterizer.enabled());
    rasterizer.render(world);

    rasterizer.shutdown();
    assert(!rasterizer.initialized());

    Models::clearCache();
    Display.destroy();
    return 0;
}
