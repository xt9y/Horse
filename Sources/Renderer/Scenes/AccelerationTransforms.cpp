#include "Renderer/Scenes/Acceleration.hpp"

#include "Models/Internal/MeshRevision.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Renderer::Scenes {
namespace {

constexpr std::uint32_t InvalidNode = UINT32_MAX;

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

bool updateInstance(
    const Ecs::World& world,
    const Scene::RenderItem& item,
    const std::vector<AccelerationBlas>& blases,
    const std::unordered_map<std::uint64_t, std::size_t>& slots,
    std::vector<AccelerationInstance>& instances,
    std::vector<std::size_t>& dirty_slots)
{
    if (!AccelerationScene::eligible(world, item)) return false;

    const auto found = slots.find(instanceKey(item.entity, item.instance_index));
    if (found == slots.end() || found->second >= instances.size()) return false;

    AccelerationInstance& instance = instances[found->second];
    if (instance.blasIndex() >= blases.size()) return false;
    const AccelerationBlas& blas = blases[instance.blasIndex()];
    if (blas.mesh != item.mesh_component->mesh ||
        blas.mesh_revision != Models::Internal::meshRevision(item.mesh_component->mesh) ||
        instance.material() != item.mesh_component->material)
        return false;

    const Math::Mat4 object_to_world = Math::modelMatrix(*item.transform);
    if (object_to_world == instance.object_to_world) return true;

    const Math::Mat4 world_to_object = Math::inverseModelMatrix(*item.transform);
    const auto [minimum, maximum] = worldBounds(item.mesh->bounds, object_to_world);
    instance.object_to_world = object_to_world;
    instance.world_to_object = world_to_object;
    instance.bounds_min = {minimum.x, minimum.y, minimum.z, 0.0f};
    instance.bounds_max = {maximum.x, maximum.y, maximum.z, 0.0f};
    dirty_slots.push_back(found->second);
    return true;
}

} // namespace

void AccelerationScene::rebuildTlasMetadata()
{
    tlas_parents_.assign(tlas_nodes_.size(), InvalidNode);
    instance_leaves_.assign(instances_.size(), InvalidNode);
    tlas_dirty_marks_.assign(tlas_nodes_.size(), 0u);
    tlas_dirty_generation_ = 0u;

    for (std::size_t node_index = 0u; node_index < tlas_nodes_.size(); ++node_index) {
        const GpuNode& node = tlas_nodes_[node_index];
        if ((node.meta & LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~LeafBit;
            if (count == 0u || static_cast<std::size_t>(node.first) + count > instances_.size()) {
                tlas_parents_.clear();
                instance_leaves_.clear();
                tlas_dirty_marks_.clear();
                tlas_metadata_signature_ = 0u;
                return;
            }
            for (std::uint32_t local = 0u; local < count; ++local)
                instance_leaves_[node.first + local] = static_cast<std::uint32_t>(node_index);
            continue;
        }

        if (node.first >= tlas_nodes_.size() || node.meta >= tlas_nodes_.size()) {
            tlas_parents_.clear();
            instance_leaves_.clear();
            tlas_dirty_marks_.clear();
            tlas_metadata_signature_ = 0u;
            return;
        }
        tlas_parents_[node.first] = static_cast<std::uint32_t>(node_index);
        tlas_parents_[node.meta] = static_cast<std::uint32_t>(node_index);
    }

    tlas_metadata_signature_ = tlas_topology_signature_;
}

bool AccelerationScene::refitDirtyTlas(const std::vector<std::size_t>& dirty_slots)
{
    if (dirty_slots.empty()) return true;
    if (tlas_nodes_.empty() || instances_.empty()) return false;

    if (tlas_metadata_signature_ != tlas_topology_signature_ ||
        tlas_parents_.size() != tlas_nodes_.size() ||
        instance_leaves_.size() != instances_.size() ||
        tlas_dirty_marks_.size() != tlas_nodes_.size())
        rebuildTlasMetadata();

    if (tlas_parents_.size() != tlas_nodes_.size() ||
        instance_leaves_.size() != instances_.size() ||
        tlas_dirty_marks_.size() != tlas_nodes_.size())
        return false;

    ++tlas_dirty_generation_;
    if (tlas_dirty_generation_ == 0u) {
        std::fill(tlas_dirty_marks_.begin(), tlas_dirty_marks_.end(), 0u);
        tlas_dirty_generation_ = 1u;
    }

    std::vector<std::uint32_t> dirty_nodes;
    dirty_nodes.reserve(dirty_slots.size() * 8u);
    for (const std::size_t slot : dirty_slots) {
        if (slot >= instance_leaves_.size()) return false;
        std::uint32_t node = instance_leaves_[slot];
        if (node == InvalidNode) return false;

        while (node != InvalidNode && tlas_dirty_marks_[node] != tlas_dirty_generation_) {
            tlas_dirty_marks_[node] = tlas_dirty_generation_;
            dirty_nodes.push_back(node);
            node = tlas_parents_[node];
        }
    }

    std::sort(dirty_nodes.begin(), dirty_nodes.end(), std::greater<std::uint32_t>());
    const float infinity = std::numeric_limits<float>::infinity();
    for (const std::uint32_t node_index : dirty_nodes) {
        if (node_index >= tlas_nodes_.size()) return false;
        GpuNode& node = tlas_nodes_[node_index];
        Vec3 minimum{infinity, infinity, infinity};
        Vec3 maximum{-infinity, -infinity, -infinity};

        if ((node.meta & LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~LeafBit;
            if (count == 0u || static_cast<std::size_t>(node.first) + count > instances_.size())
                return false;
            for (std::uint32_t local = 0u; local < count; ++local) {
                const AccelerationInstance& instance = instances_[node.first + local];
                minimum = minVec(minimum, instanceMinimum(instance));
                maximum = maxVec(maximum, instanceMaximum(instance));
            }
        } else {
            if (node.first >= tlas_nodes_.size() || node.meta >= tlas_nodes_.size()) return false;
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
    return true;
}

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
        if (!updateInstance(world, item, blases_, instance_slots_, instances_, dirty_slots))
            return sync(world, items, error);
    }

    if (eligible_count != instances_.size()) return sync(world, items, error);
    if (!refitDirtyTlas(dirty_slots)) return sync(world, items, error);
    if (dirty_slots.empty()) return true;

    tlas_signature_ = 0u;
    ++tlas_revision_;
    return true;
}

bool AccelerationScene::syncTransforms(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    const std::vector<std::size_t>& changed_items,
    std::string *error)
{
    if (error) error->clear();
    if (changed_items.empty()) return true;
    if (instances_.empty() || tlas_nodes_.empty() || instance_slots_.size() != instances_.size())
        return sync(world, items, error);

    std::vector<std::size_t> dirty_slots;
    dirty_slots.reserve(changed_items.size());
    for (const std::size_t item_index : changed_items) {
        if (item_index >= items.size()) return sync(world, items, error);
        if (!updateInstance(
                world,
                items[item_index],
                blases_,
                instance_slots_,
                instances_,
                dirty_slots))
            return sync(world, items, error);
    }

    if (!refitDirtyTlas(dirty_slots)) return sync(world, items, error);
    if (dirty_slots.empty()) return true;

    tlas_signature_ = 0u;
    ++tlas_revision_;
    return true;
}

} // namespace Renderer::Scenes
