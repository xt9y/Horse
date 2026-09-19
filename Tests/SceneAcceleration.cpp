#include <Ecs/Ecs.hpp>
#include <Models/Internal/Registry.hpp>
#include <Models/Models.hpp>
#include <Renderer/Scenes/Acceleration.hpp>
#include <Renderer/Scenes/Scene.hpp>

#include <cassert>
#include <string>
#include <vector>

namespace {

Renderer::Scenes::Scene::RenderItem itemFor(
    Ecs::Entity entity,
    Models::MeshHandle mesh,
    float x)
{
    Renderer::Scenes::Scene::RenderItem item;
    item.entity = entity;
    item.instance_index = 0u;
    item.layer = Renderer::RenderLayer::World;
    item.transform.valid = true;
    item.transform.value.position = {x, 0.0f, 0.0f};
    item.mesh_component = Renderer::Scenes::Scene::MeshBinding{
        mesh,
        Models::INVALID_MATERIAL,
        true,
    };
    item.mesh = Models::mesh(mesh);
    return item;
}

Models::MeshData triangleMesh()
{
    Models::MeshData mesh;
    mesh.vertices = {
        Models::Vertex{{-1.0f, 0.0f, 0.0f}},
        Models::Vertex{{1.0f, 0.0f, 0.0f}},
        Models::Vertex{{0.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.bounds.minimum = {-1.0f, 0.0f, 0.0f};
    mesh.bounds.maximum = {1.0f, 1.0f, 0.0f};
    return mesh;
}

} // namespace

int main()
{
    Models::clearCache();
    const Models::MeshHandle mesh = Models::Internal::registerMesh(triangleMesh());
    assert(mesh != Models::INVALID_MESH);

    Ecs::World world;
    const Ecs::Entity first = world.createEntity();
    const Ecs::Entity second = world.createEntity();

    std::vector<Renderer::Scenes::Scene::RenderItem> items;
    items.push_back(itemFor(first, mesh, -2.0f));
    items.push_back(itemFor(second, mesh, 2.0f));

    Renderer::Scenes::AccelerationScene acceleration;
    std::string error;
    assert(acceleration.sync(world, items, &error));
    assert(error.empty());
    assert(acceleration.blases().size() == 1u);
    assert(acceleration.instances().size() == 2u);
    assert(acceleration.localTriangles().size() == 1u);
    assert(!acceleration.tlasNodes().empty());

    const std::uint64_t blas_revision = acceleration.blasRevision();
    const std::uint64_t tlas_revision = acceleration.tlasRevision();

    items[1].transform.value.position.x = 4.0f;
    assert(acceleration.sync(world, items, &error));
    assert(error.empty());
    assert(acceleration.blasRevision() == blas_revision);
    assert(acceleration.tlasRevision() > tlas_revision);
    assert(acceleration.blases().size() == 1u);
    assert(acceleration.instances().size() == 2u);

    Models::MeshData changed = triangleMesh();
    changed.vertices[2].position.y = 2.0f;
    changed.bounds.maximum.y = 2.0f;
    assert(Models::Internal::updateMesh(mesh, changed));
    items[0].mesh = Models::mesh(mesh);
    items[1].mesh = Models::mesh(mesh);

    const std::uint64_t previous_blas_revision = acceleration.blasRevision();
    assert(acceleration.sync(world, items, &error));
    assert(error.empty());
    assert(acceleration.blasRevision() > previous_blas_revision);
    assert(acceleration.blases().size() == 1u);
    assert(acceleration.localTriangles().size() == 1u);

    Models::clearCache();
    return 0;
}
