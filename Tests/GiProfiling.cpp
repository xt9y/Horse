#include <Core/Jobs/Jobs.hpp>
#include <Ecs/Ecs.hpp>
#include <Models/Formats/Registry.hpp>
#include <Models/Internal/Registry.hpp>
#include <Models/Models.hpp>
#include <Renderer/GlobalIllumination/Debug.hpp>
#include <Renderer/GlobalIllumination/TraceScene.hpp>
#include <Renderer/ModelScene.hpp>
#include <Renderer/Scenes/Acceleration.hpp>
#include <Renderer/Scenes/Scene.hpp>
#include <Renderer/Scenes/SceneCache.hpp>

#include <cassert>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace {

bool loadTraceScene(
    const std::string&,
    Models::Formats::Document *document,
    std::string *error)
{
    if (error) error->clear();
    if (!document) return false;

    Models::Formats::Part part;
    part.node = 0u;
    part.mesh.vertices.reserve(48u);
    part.mesh.indices.reserve(48u);

    for (std::uint32_t triangle = 0u; triangle < 16u; ++triangle) {
        const float z = -2.0f - static_cast<float>(triangle) * 0.25f;
        const std::uint32_t base = static_cast<std::uint32_t>(part.mesh.vertices.size());
        part.mesh.vertices.push_back({{0.0f, 0.0f, z}});
        part.mesh.vertices.push_back({{1.0f, 0.0f, z}});
        part.mesh.vertices.push_back({{0.0f, 1.0f, z}});
        part.mesh.indices.push_back(base + 0u);
        part.mesh.indices.push_back(base + 1u);
        part.mesh.indices.push_back(base + 2u);
    }
    part.mesh.bounds.minimum = {0.0f, 0.0f, -5.75f};
    part.mesh.bounds.maximum = {1.0f, 1.0f, -2.0f};
    document->parts.push_back(std::move(part));

    Models::NodeData node;
    node.name = "trace-node";
    node.parts = {0u};
    document->nodes.push_back(std::move(node));

    Models::SceneData scene;
    scene.name = "trace-scene";
    scene.nodes = {0u};
    document->scenes.push_back(std::move(scene));
    document->default_scene = 0u;
    return true;
}

Renderer::Scenes::Scene::RenderItem accelerationItem(
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

Models::MeshData accelerationMesh()
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
    using Renderer::GlobalIllumination::Debug::SceneUpdate;
    using Renderer::GlobalIllumination::Debug::Statistics;

    Statistics stats;
    assert(stats.scene_update == SceneUpdate::None);
    assert(stats.topology_updates == 0u);
    assert(stats.geometry_updates == 0u);
    assert(stats.resource_updates == 0u);
    assert(stats.scene_build_ms == 0.0);
    assert(stats.photon_build_ms == 0.0);
    assert(stats.probe_update_ms == 0.0);

    assert(Renderer::Scenes::SceneCache::leafSize() == 8u);

    Models::clearCache();
    assert(Models::Formats::registerLoader(".tracescene", loadTraceScene));

    std::string error;
    const Models::ModelHandle model = Models::load("gi-traversal.tracescene", &error);
    assert(model != Models::INVALID_MODEL);
    assert(error.empty());

    Ecs::World world;
    Renderer::ModelScene::Instance instance;
    assert(Renderer::ModelScene::instantiate(world, model, &instance, {}, &error));
    assert(error.empty());

    std::vector<Renderer::Scenes::Scene::RenderItem> items;
    Renderer::Scenes::Scene::collectRenderItems(world, items);
    assert(!items.empty());

    Renderer::GlobalIllumination::TraceScene trace_scene;
    assert(trace_scene.build(world, items, &error));
    assert(error.empty());
    assert(trace_scene.cache().nodes().size() > 1u);

    const Renderer::GlobalIllumination::TraceHit hit = trace_scene.traceClosest(
        {0.25f, 0.25f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        20.0f,
        1.0e-4f);
    assert(hit.found);
    assert(std::abs(hit.distance - 2.0f) < 1.0e-4f);

    assert(trace_scene.occluded(
        {0.25f, 0.25f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        20.0f,
        1.0e-4f));
    assert(!trace_scene.occluded(
        {2.0f, 2.0f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        20.0f,
        1.0e-4f));

    Renderer::ModelScene::destroy(world, instance);
    Models::clearCache();

    const Models::MeshHandle mesh = Models::Internal::registerMesh(accelerationMesh());
    assert(mesh != Models::INVALID_MESH);
    Ecs::World acceleration_world;
    const Ecs::Entity first = acceleration_world.createEntity();
    const Ecs::Entity second = acceleration_world.createEntity();
    std::vector<Renderer::Scenes::Scene::RenderItem> acceleration_items;
    acceleration_items.push_back(accelerationItem(first, mesh, -2.0f));
    acceleration_items.push_back(accelerationItem(second, mesh, 2.0f));

    Renderer::Scenes::AccelerationScene acceleration;
    assert(acceleration.sync(acceleration_world, acceleration_items, &error));
    assert(error.empty());
    assert(acceleration.blases().size() == 1u);
    assert(acceleration.instances().size() == 2u);
    assert(acceleration.localTriangles().size() == 1u);
    assert(!acceleration.tlasNodes().empty());

    const std::uint64_t blas_revision = acceleration.blasRevision();
    const std::uint64_t tlas_revision = acceleration.tlasRevision();
    acceleration_items[1].transform.value.position.x = 4.0f;
    assert(acceleration.sync(acceleration_world, acceleration_items, &error));
    assert(error.empty());
    assert(acceleration.blasRevision() == blas_revision);
    assert(acceleration.tlasRevision() > tlas_revision);

    Models::MeshData changed = accelerationMesh();
    changed.vertices[2].position.y = 2.0f;
    changed.bounds.maximum.y = 2.0f;
    assert(Models::Internal::updateMesh(mesh, changed));
    acceleration_items[0].mesh = Models::mesh(mesh);
    acceleration_items[1].mesh = Models::mesh(mesh);
    const std::uint64_t previous_blas_revision = acceleration.blasRevision();
    assert(acceleration.sync(acceleration_world, acceleration_items, &error));
    assert(error.empty());
    assert(acceleration.blasRevision() > previous_blas_revision);
    assert(acceleration.blases().size() == 1u);

    Models::clearCache();
    Core::Jobs::shutdown();
    return 0;
}
