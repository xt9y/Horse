#include "Renderer/Scenes/Acceleration.hpp"

#include "Animation/Animation.hpp"
#include "Models/Internal/MeshRevision.hpp"
#include "Renderer/Internal/ModelGeometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Renderer::Scenes {
namespace {

constexpr std::uint32_t BlasLeafSize = 8u;
constexpr std::uint32_t TlasLeafSize = 4u;

Vec3 minVec(Vec3 a, Vec3 b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec(Vec3 a, Vec3 b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float component(Vec3 value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

Vec3 triangleCentroid(const AccelerationTriangle& triangle)
{
    return {
        (triangle.p0[0] + triangle.p1[0] + triangle.p2[0]) / 3.0f,
        (triangle.p0[1] + triangle.p1[1] + triangle.p2[1]) / 3.0f,
        (triangle.p0[2] + triangle.p1[2] + triangle.p2[2]) / 3.0f,
    };
}

std::uint32_t buildBlasNode(
    std::vector<GpuNode>& nodes,
    const std::vector<AccelerationTriangle>& triangles,
    std::vector<std::uint32_t>& order,
    std::uint32_t start,
    std::uint32_t count)
{
    const float infinity = std::numeric_limits<float>::infinity();
    Vec3 bounds_min{infinity, infinity, infinity};
    Vec3 bounds_max{-infinity, -infinity, -infinity};
    Vec3 centroid_min{infinity, infinity, infinity};
    Vec3 centroid_max{-infinity, -infinity, -infinity};

    for (std::uint32_t index = 0u; index < count; ++index) {
        const AccelerationTriangle& triangle = triangles[order[start + index]];
        const Vec3 p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
        const Vec3 p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
        const Vec3 p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
        bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
        bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
        const Vec3 centroid = triangleCentroid(triangle);
        centroid_min = minVec(centroid_min, centroid);
        centroid_max = maxVec(centroid_max, centroid);
    }

    const std::uint32_t node_index = static_cast<std::uint32_t>(nodes.size());
    GpuNode node;
    node.min_x = bounds_min.x;
    node.min_y = bounds_min.y;
    node.min_z = bounds_min.z;
    node.max_x = bounds_max.x;
    node.max_y = bounds_max.y;
    node.max_z = bounds_max.z;
    nodes.push_back(node);

    const Vec3 extent = subtract(centroid_max, centroid_min);
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > component(extent, axis)) axis = 2;

    if (count <= BlasLeafSize || component(extent, axis) <= 1.0e-6f) {
        nodes[node_index].first = start;
        nodes[node_index].meta = LeafBit | count;
        return node_index;
    }

    const std::uint32_t left_count = count / 2u;
    const std::uint32_t middle = start + left_count;
    std::nth_element(
        order.begin() + start,
        order.begin() + middle,
        order.begin() + start + count,
        [&](std::uint32_t a, std::uint32_t b) {
            return component(triangleCentroid(triangles[a]), axis) <
                component(triangleCentroid(triangles[b]), axis);
        }
    );

    const std::uint32_t left = buildBlasNode(nodes, triangles, order, start, left_count);
    const std::uint32_t right = buildBlasNode(
        nodes,
        triangles,
        order,
        middle,
        count - left_count
    );
    nodes[node_index].first = left;
    nodes[node_index].meta = right;
    return node_index;
}

Vec3 instanceMinimum(const AccelerationInstance& instance)
{
    return {instance.bounds_min[0], instance.bounds_min[1], instance.bounds_min[2]};
}

Vec3 instanceMaximum(const AccelerationInstance& instance)
{
    return {instance.bounds_max[0], instance.bounds_max[1], instance.bounds_max[2]};
}

Vec3 instanceCentroid(const AccelerationInstance& instance)
{
    const Vec3 minimum = instanceMinimum(instance);
    const Vec3 maximum = instanceMaximum(instance);
    return {
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f,
        (minimum.z + maximum.z) * 0.5f,
    };
}

std::uint32_t buildTlasNode(
    std::vector<GpuNode>& nodes,
    const std::vector<AccelerationInstance>& instances,
    std::vector<std::uint32_t>& order,
    std::uint32_t start,
    std::uint32_t count)
{
    const float infinity = std::numeric_limits<float>::infinity();
    Vec3 bounds_min{infinity, infinity, infinity};
    Vec3 bounds_max{-infinity, -infinity, -infinity};
    Vec3 centroid_min{infinity, infinity, infinity};
    Vec3 centroid_max{-infinity, -infinity, -infinity};

    for (std::uint32_t index = 0u; index < count; ++index) {
        const AccelerationInstance& instance = instances[order[start + index]];
        bounds_min = minVec(bounds_min, instanceMinimum(instance));
        bounds_max = maxVec(bounds_max, instanceMaximum(instance));
        const Vec3 centroid = instanceCentroid(instance);
        centroid_min = minVec(centroid_min, centroid);
        centroid_max = maxVec(centroid_max, centroid);
    }

    const std::uint32_t node_index = static_cast<std::uint32_t>(nodes.size());
    GpuNode node;
    node.min_x = bounds_min.x;
    node.min_y = bounds_min.y;
    node.min_z = bounds_min.z;
    node.max_x = bounds_max.x;
    node.max_y = bounds_max.y;
    node.max_z = bounds_max.z;
    nodes.push_back(node);

    const Vec3 extent = subtract(centroid_max, centroid_min);
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > component(extent, axis)) axis = 2;

    if (count <= TlasLeafSize || component(extent, axis) <= 1.0e-6f) {
        nodes[node_index].first = start;
        nodes[node_index].meta = LeafBit | count;
        return node_index;
    }

    const std::uint32_t left_count = count / 2u;
    const std::uint32_t middle = start + left_count;
    std::nth_element(
        order.begin() + start,
        order.begin() + middle,
        order.begin() + start + count,
        [&](std::uint32_t a, std::uint32_t b) {
            return component(instanceCentroid(instances[a]), axis) <
                component(instanceCentroid(instances[b]), axis);
        }
    );

    const std::uint32_t left = buildTlasNode(nodes, instances, order, start, left_count);
    const std::uint32_t right = buildTlasNode(
        nodes,
        instances,
        order,
        middle,
        count - left_count
    );
    nodes[node_index].first = left;
    nodes[node_index].meta = right;
    return node_index;
}

void hashValue(std::uint64_t& hash, std::uint32_t value)
{
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ull;
}

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hashValue(hash, static_cast<std::uint32_t>(value));
    hashValue(hash, static_cast<std::uint32_t>(value >> 32u));
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

void hashMatrix(std::uint64_t& hash, const Math::Mat4& matrix)
{
    for (float value : matrix) hashFloat(hash, value);
}

std::pair<Vec3, Vec3> worldBounds(const Models::Bounds& bounds, const Math::Mat4& matrix)
{
    const float infinity = std::numeric_limits<float>::infinity();
    Vec3 minimum{infinity, infinity, infinity};
    Vec3 maximum{-infinity, -infinity, -infinity};

    for (std::uint32_t corner = 0u; corner < 8u; ++corner) {
        const Vec3 local{
            (corner & 1u) != 0u ? bounds.maximum.x : bounds.minimum.x,
            (corner & 2u) != 0u ? bounds.maximum.y : bounds.minimum.y,
            (corner & 4u) != 0u ? bounds.maximum.z : bounds.minimum.z,
        };
        const Vec3 world = Math::transformPoint(matrix, local);
        minimum = minVec(minimum, world);
        maximum = maxVec(maximum, world);
    }
    return {minimum, maximum};
}

} // namespace

bool AccelerationScene::eligible(const Ecs::World& world, const Scene::RenderItem& item)
{
    if (item.layer != RenderLayer::World || !item.mesh_component || !item.transform || !item.mesh)
        return false;
    if (item.mesh->vertices.empty() || item.mesh->indices.size() < 3u) return false;
    if (item.material && item.material->opacity < SceneCache::opacityCutoff()) return false;
    if (world.get<Internal::ModelDeformComponent>(item.entity)) return false;
    if (world.get<Animation::SkinBindingComponent>(item.entity)) return false;
    return true;
}

bool AccelerationScene::buildBlas(
    Models::MeshHandle handle,
    CachedBlas *out,
    std::string *error)
{
    if (!out) return false;
    const Models::MeshData *mesh = Models::mesh(handle);
    if (!mesh) {
        if (error) *error = "acceleration BLAS references an invalid mesh";
        return false;
    }

    CachedBlas result;
    result.mesh = handle;
    result.mesh_revision = Models::Internal::meshRevision(handle);
    result.triangles.reserve(mesh->indices.size() / 3u);

    for (std::size_t index = 0u; index + 2u < mesh->indices.size(); index += 3u) {
        const std::uint32_t i0 = mesh->indices[index + 0u];
        const std::uint32_t i1 = mesh->indices[index + 1u];
        const std::uint32_t i2 = mesh->indices[index + 2u];
        if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) {
            if (error) *error = "acceleration BLAS mesh contains an invalid triangle index";
            return false;
        }

        const Models::Vertex& v0 = mesh->vertices[i0];
        const Models::Vertex& v1 = mesh->vertices[i1];
        const Models::Vertex& v2 = mesh->vertices[i2];
        AccelerationTriangle triangle;
        triangle.p0 = {v0.position.x, v0.position.y, v0.position.z, 0.0f};
        triangle.p1 = {v1.position.x, v1.position.y, v1.position.z, 0.0f};
        triangle.p2 = {v2.position.x, v2.position.y, v2.position.z, 0.0f};
        triangle.n0 = {v0.normal.x, v0.normal.y, v0.normal.z, 0.0f};
        triangle.n1 = {v1.normal.x, v1.normal.y, v1.normal.z, 0.0f};
        triangle.n2 = {v2.normal.x, v2.normal.y, v2.normal.z, 0.0f};
        triangle.uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
        triangle.uv2 = {v2.uv.x, v2.uv.y, 0.0f, 0.0f};
        result.triangles.push_back(triangle);
    }

    if (!result.triangles.empty()) {
        std::vector<std::uint32_t> order(result.triangles.size());
        for (std::size_t index = 0u; index < order.size(); ++index)
            order[index] = static_cast<std::uint32_t>(index);
        result.nodes.reserve(result.triangles.size() * 2u);
        buildBlasNode(
            result.nodes,
            result.triangles,
            order,
            0u,
            static_cast<std::uint32_t>(order.size())
        );

        std::vector<AccelerationTriangle> unsorted = std::move(result.triangles);
        result.triangles.resize(order.size());
        for (std::size_t index = 0u; index < order.size(); ++index)
            result.triangles[index] = std::move(unsorted[order[index]]);
    }

    *out = std::move(result);
    return true;
}

void AccelerationScene::flattenBlases(const std::vector<Models::MeshHandle>& meshes)
{
    blas_nodes_.clear();
    local_triangles_.clear();
    blases_.clear();
    blases_.reserve(meshes.size());

    for (Models::MeshHandle mesh : meshes) {
        const auto found = blas_cache_.find(mesh);
        if (found == blas_cache_.end()) continue;
        const CachedBlas& cached = found->second;

        AccelerationBlas blas;
        blas.mesh = mesh;
        blas.node_offset = static_cast<std::uint32_t>(blas_nodes_.size());
        blas.node_count = static_cast<std::uint32_t>(cached.nodes.size());
        blas.triangle_offset = static_cast<std::uint32_t>(local_triangles_.size());
        blas.triangle_count = static_cast<std::uint32_t>(cached.triangles.size());
        blas.mesh_revision = cached.mesh_revision;

        for (GpuNode node : cached.nodes) {
            if ((node.meta & LeafBit) != 0u) {
                node.first += blas.triangle_offset;
            } else {
                node.first += blas.node_offset;
                node.meta += blas.node_offset;
            }
            blas_nodes_.push_back(node);
        }
        local_triangles_.insert(
            local_triangles_.end(),
            cached.triangles.begin(),
            cached.triangles.end()
        );
        blases_.push_back(blas);
    }
}

void AccelerationScene::rebuildTlas(std::vector<AccelerationInstance> source)
{
    tlas_nodes_.clear();
    instances_.clear();
    if (source.empty()) return;

    std::vector<std::uint32_t> order(source.size());
    for (std::size_t index = 0u; index < order.size(); ++index)
        order[index] = static_cast<std::uint32_t>(index);
    tlas_nodes_.reserve(source.size() * 2u);
    buildTlasNode(
        tlas_nodes_,
        source,
        order,
        0u,
        static_cast<std::uint32_t>(order.size())
    );

    instances_.resize(order.size());
    for (std::size_t index = 0u; index < order.size(); ++index)
        instances_[index] = std::move(source[order[index]]);
}

bool AccelerationScene::sync(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::string *error)
{
    if (error) error->clear();

    std::vector<Models::MeshHandle> meshes;
    meshes.reserve(items.size());
    for (const Scene::RenderItem& item : items) {
        if (!eligible(world, item)) continue;
        meshes.push_back(item.mesh_component->mesh);
    }
    std::sort(meshes.begin(), meshes.end());
    meshes.erase(std::unique(meshes.begin(), meshes.end()), meshes.end());

    bool blas_changed = false;
    const std::unordered_set<Models::MeshHandle> required(meshes.begin(), meshes.end());
    for (auto it = blas_cache_.begin(); it != blas_cache_.end();) {
        if (required.find(it->first) == required.end()) {
            it = blas_cache_.erase(it);
            blas_changed = true;
        } else {
            ++it;
        }
    }

    for (Models::MeshHandle mesh : meshes) {
        const std::uint64_t revision = Models::Internal::meshRevision(mesh);
        const auto found = blas_cache_.find(mesh);
        if (found != blas_cache_.end() && found->second.mesh_revision == revision) continue;

        CachedBlas rebuilt;
        if (!buildBlas(mesh, &rebuilt, error)) return false;
        blas_cache_[mesh] = std::move(rebuilt);
        blas_changed = true;
    }

    if (blas_changed || blases_.size() != meshes.size()) {
        flattenBlases(meshes);
        ++blas_revision_;
    }

    std::unordered_map<Models::MeshHandle, std::uint32_t> blas_indices;
    blas_indices.reserve(blases_.size());
    for (std::size_t index = 0u; index < blases_.size(); ++index)
        blas_indices.emplace(blases_[index].mesh, static_cast<std::uint32_t>(index));

    std::vector<AccelerationInstance> next_instances;
    next_instances.reserve(items.size());
    std::uint64_t signature = 1469598103934665603ull;
    hashValue(signature, blas_revision_);

    for (const Scene::RenderItem& item : items) {
        if (!eligible(world, item)) continue;
        const auto blas = blas_indices.find(item.mesh_component->mesh);
        if (blas == blas_indices.end()) continue;

        const Math::Mat4 object_to_world = Math::modelMatrix(*item.transform);
        const Math::Mat4 world_to_object = Math::inverseModelMatrix(*item.transform);
        const auto [minimum, maximum] = worldBounds(item.mesh->bounds, object_to_world);

        AccelerationInstance instance;
        instance.object_to_world = object_to_world;
        instance.world_to_object = world_to_object;
        instance.bounds_min = {minimum.x, minimum.y, minimum.z, 0.0f};
        instance.bounds_max = {maximum.x, maximum.y, maximum.z, 0.0f};
        instance.data = {
            item.entity,
            item.instance_index,
            blas->second,
            item.mesh_component->material,
        };
        next_instances.push_back(instance);

        hashValue(signature, item.entity);
        hashValue(signature, item.instance_index);
        hashValue(signature, item.mesh_component->mesh);
        hashValue(signature, item.mesh_component->material);
        hashMatrix(signature, object_to_world);
    }

    if (signature != tlas_signature_) {
        rebuildTlas(std::move(next_instances));
        tlas_signature_ = signature;
        ++tlas_revision_;
    }
    return true;
}

void AccelerationScene::clear()
{
    blas_cache_.clear();
    tlas_nodes_.clear();
    blas_nodes_.clear();
    local_triangles_.clear();
    blases_.clear();
    instances_.clear();
    blas_revision_ = 0u;
    tlas_revision_ = 0u;
    tlas_signature_ = 0u;
}

} // namespace Renderer::Scenes
