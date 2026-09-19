#include "Renderer/Scenes/Acceleration.hpp"

#include "Models/Internal/MeshRevision.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Renderer::Scenes {
namespace {

Vec3 minVec(Vec3 a, Vec3 b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec(Vec3 a, Vec3 b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

Vec3 instanceMinimum(const AccelerationInstance& instance)
{
    return {instance.bounds_min[0], instance.bounds_min[1], instance.bounds_min[2]};
}

Vec3 instanceMaximum(const AccelerationInstance& instance)
{
    return {instance.bounds_max[0], instance.bounds_max[1], instance.bounds_max[2]};
}

std::uint64_t instanceKey(Ecs::Entity entity, std::uint32_t instance_index)
{
    return (static_cast<std::uint64_t>(entity) << 32u) |
        static_cast<std::uint64_t>(instance_index);
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

void setBounds(GpuNode& node, Vec3 minimum, Vec3 maximum)
{
    node.min_x = minimum.x;
    node.min_y = minimum.y;
    node.min_z = minimum.z;
    node.max_x = maximum.x;
    node.max_y = maximum.y;
    node.max_z = maximum.z;
}

} // namespace

bool AccelerationScene::syncTransforms(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::string *error)
{
    if (error) error->clear();

    if (instances_.empty()) {
        for (const Scene::RenderItem& item : items) {
            if (eligible(world, item)) return sync(world, items, error);
        }
        return true;
    }
    if (tlas_nodes_.empty() || instance_slots_.size() != instances_.size())
        return sync(world, items, error);

    std::vector<std::size_t> dirty_slots;
    dirty_slots.reserve(4u);
    std::size_t eligible_count = 0u;

    for (const Scene::RenderItem& item : items) {
        if (!eligible(world, item)) continue;
        ++eligible_count;

        const auto found = instance_slots_.find(instanceKey(item.entity, item.instance_index));
        if (found == instance_slots_.end() || found->second >= instances_.size())
            return sync(world, items, error);

        AccelerationInstance& instance = instances_[found->second];
        if (instance.blasIndex() >= blases_.size()) return sync(world, items, error);
        const AccelerationBlas& blas = blases_[instance.blasIndex()];
        if (blas.mesh != item.mesh_component->mesh ||
            blas.mesh_revision != Models::Internal::meshRevision(item.mesh_component->mesh) ||
            instance.material() != item.mesh_component->material)
            return sync(world, items, error);

        const Math::Mat4 object_to_world = Math::modelMatrix(*item.transform);
        if (object_to_world == instance.object_to_world) continue;

        const Math::Mat4 world_to_object = Math::inverseModelMatrix(*item.transform);
        const auto [minimum, maximum] = worldBounds(item.mesh->bounds, object_to_world);
        instance.object_to_world = object_to_world;
        instance.world_to_object = world_to_object;
        instance.bounds_min = {minimum.x, minimum.y, minimum.z, 0.0f};
        instance.bounds_max = {maximum.x, maximum.y, maximum.z, 0.0f};
        dirty_slots.push_back(found->second);
    }

    if (eligible_count != instances_.size()) return sync(world, items, error);
    if (dirty_slots.empty()) return true;

    constexpr std::uint32_t InvalidNode = UINT32_MAX;
    std::vector<std::uint32_t> parents(tlas_nodes_.size(), InvalidNode);
    std::vector<std::uint32_t> leaves(instances_.size(), InvalidNode);

    for (std::size_t node_index = 0u; node_index < tlas_nodes_.size(); ++node_index) {
        const GpuNode& node = tlas_nodes_[node_index];
        if ((node.meta & LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~LeafBit;
            if (count == 0u || static_cast<std::size_t>(node.first) + count > instances_.size())
                return sync(world, items, error);
            for (std::uint32_t local = 0u; local < count; ++local)
                leaves[node.first + local] = static_cast<std::uint32_t>(node_index);
            continue;
        }

        if (node.first >= tlas_nodes_.size() || node.meta >= tlas_nodes_.size())
            return sync(world, items, error);
        parents[node.first] = static_cast<std::uint32_t>(node_index);
        parents[node.meta] = static_cast<std::uint32_t>(node_index);
    }

    std::vector<std::uint8_t> dirty_nodes(tlas_nodes_.size(), 0u);
    for (const std::size_t slot : dirty_slots) {
        if (slot >= leaves.size() || leaves[slot] == InvalidNode)
            return sync(world, items, error);

        std::uint32_t node = leaves[slot];
        while (node != InvalidNode && dirty_nodes[node] == 0u) {
            dirty_nodes[node] = 1u;
            node = parents[node];
        }
    }

    const float infinity = std::numeric_limits<float>::infinity();
    for (std::size_t reverse = tlas_nodes_.size(); reverse > 0u; --reverse) {
        const std::size_t node_index = reverse - 1u;
        if (dirty_nodes[node_index] == 0u) continue;

        GpuNode& node = tlas_nodes_[node_index];
        Vec3 minimum{infinity, infinity, infinity};
        Vec3 maximum{-infinity, -infinity, -infinity};
        if ((node.meta & LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~LeafBit;
            for (std::uint32_t local = 0u; local < count; ++local) {
                const AccelerationInstance& instance = instances_[node.first + local];
                minimum = minVec(minimum, instanceMinimum(instance));
                maximum = maxVec(maximum, instanceMaximum(instance));
            }
        } else {
            const GpuNode& left = tlas_nodes_[node.first];
            const GpuNode& right = tlas_nodes_[node.meta];
            minimum = minVec(
                {left.min_x, left.min_y, left.min_z},
                {right.min_x, right.min_y, right.min_z});
            maximum = maxVec(
                {left.max_x, left.max_y, left.max_z},
                {right.max_x, right.max_y, right.max_z});
        }
        setBounds(node, minimum, maximum);
    }

    tlas_signature_ = 0u;
    ++tlas_revision_;
    return true;
}

} // namespace Renderer::Scenes
