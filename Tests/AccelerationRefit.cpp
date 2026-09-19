#include <Ecs/Ecs.hpp>
#include <Models/Internal/Registry.hpp>
#include <Models/Models.hpp>
#include <Renderer/Scenes/Acceleration.hpp>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

Models::MeshData meshData()
{
    Models::MeshData mesh;
    mesh.vertices = {
        Models::Vertex{{-0.5f, -0.5f, 0.0f}},
        Models::Vertex{{ 0.5f, -0.5f, 0.0f}},
        Models::Vertex{{ 0.0f,  0.5f, 0.0f}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.bounds.minimum = {-0.5f, -0.5f, 0.0f};
    mesh.bounds.maximum = { 0.5f,  0.5f, 0.0f};
    return mesh;
}

Renderer::Scenes::Scene::RenderItem item(
    Ecs::Entity entity,
    Models::MeshHandle mesh,
    float x)
{
    Renderer::Scenes::Scene::RenderItem result;
    result.entity = entity;
    result.instance_index = 0u;
    result.layer = Renderer::RenderLayer::World;
    result.transform.valid = true;
    result.transform.value.position = {x, 0.0f, 0.0f};
    result.mesh_component = Renderer::Scenes::Scene::MeshBinding{
        mesh,
        Models::INVALID_MATERIAL,
        true,
    };
    result.mesh = Models::mesh(mesh);
    return result;
}

struct Topology {
    std::uint32_t first = 0u;
    std::uint32_t meta = 0u;
};

} // namespace

int main()
{
    Models::clearCache();
    const Models::MeshHandle mesh = Models::Internal::registerMesh(meshData());
    assert(mesh != Models::INVALID_MESH);

    Ecs::World world;
    std::vector<Renderer::Scenes::Scene::RenderItem> items;
    for (std::uint32_t index = 0u; index < 8u; ++index) {
        const Ecs::Entity entity = world.createEntity();
        items.push_back(item(entity, mesh, -7.0f + static_cast<float>(index) * 2.0f));
    }

    Renderer::Scenes::AccelerationScene acceleration;
    std::string error;
    assert(acceleration.sync(world, items, &error));
    assert(error.empty());
    assert(acceleration.instances().size() == items.size());
    assert(acceleration.tlasNodes().size() > 1u);

    std::vector<Ecs::Entity> instance_order;
    for (const Renderer::Scenes::AccelerationInstance& instance : acceleration.instances())
        instance_order.push_back(instance.entity());

    std::vector<Topology> topology;
    for (const Renderer::Scenes::GpuNode& node : acceleration.tlasNodes())
        topology.push_back({node.first, node.meta});

    const float previous_root_max = acceleration.tlasNodes().front().max_x;
    const std::uint64_t previous_blas_revision = acceleration.blasRevision();
    const std::uint64_t previous_tlas_revision = acceleration.tlasRevision();

    items.front().transform.value.position.x = 50.0f;
    assert(acceleration.sync(world, items, &error));
    assert(error.empty());

    assert(acceleration.blasRevision() == previous_blas_revision);
    assert(acceleration.tlasRevision() > previous_tlas_revision);
    assert(acceleration.instances().size() == instance_order.size());
    assert(acceleration.tlasNodes().size() == topology.size());
    assert(acceleration.tlasNodes().front().max_x > previous_root_max);

    for (std::size_t index = 0u; index < instance_order.size(); ++index)
        assert(acceleration.instances()[index].entity() == instance_order[index]);
    for (std::size_t index = 0u; index < topology.size(); ++index) {
        assert(acceleration.tlasNodes()[index].first == topology[index].first);
        assert(acceleration.tlasNodes()[index].meta == topology[index].meta);
    }

    Models::clearCache();
    return 0;
}
