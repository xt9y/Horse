#include "Renderer/Scenes/SceneCache.hpp"

#include "Animation/Animation.hpp"
#include "Camera/Camera.hpp"
#include "Models/Internal/MeshRevision.hpp"
#include "Models/Core/Texture.hpp"
#include "Models/Runtime.hpp"
#include "Renderer/Internal/ModelGeometry.hpp"
#include "Renderer/Environment.hpp"
#include "Renderer/Math.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Renderer::Scenes {
namespace {

using Math::inverseModelMatrix;
using Math::modelMatrix;
using Math::normalize;
using Math::transformNormal;
using Math::transformPoint;

constexpr std::uint32_t PackedTextureNone = 31u;
constexpr float MaximumPackedEmissiveStrength = 16.0f;

Vec3 subtract(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 minVec(const Vec3& a, const Vec3& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec(const Vec3& a, const Vec3& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

float component(const Vec3& value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
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

void hashVec3(std::uint64_t& hash, const Vec3& value)
{
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashTransform(std::uint64_t& hash, const Transform& transform)
{
    hashValue(hash, transform.matrix_override_enabled ? 1u : 0u);
    if (transform.matrix_override_enabled) {
        for (float value : transform.matrix_override) hashFloat(hash, value);
        return;
    }
    hashVec3(hash, transform.position);
    hashVec3(hash, transform.rotation);
    hashVec3(hash, transform.scale);
}

Vec3 triangleCentroid(const GpuTriangle& triangle)
{
    return {
        (triangle.p0[0] + triangle.p1[0] + triangle.p2[0]) / 3.0f,
        (triangle.p0[1] + triangle.p1[1] + triangle.p2[1]) / 3.0f,
        (triangle.p0[2] + triangle.p1[2] + triangle.p2[2]) / 3.0f,
    };
}

struct PreparedItem {
    const Scene::RenderItem *item = nullptr;
    const Models::Runtime::DeformedPart *deformed = nullptr;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::uint32_t material_index = 0u;
    bool valid = false;
};

struct DeformationKey {
    Models::ModelHandle model = Models::INVALID_MODEL;
    std::uint32_t part = Models::INVALID_INDEX;
    Ecs::Entity pose_entity = Ecs::INVALID_ENTITY;
    std::uint64_t pose_revision = 0u;

    bool operator==(const DeformationKey&) const = default;
};

struct DeformationKeyHash {
    std::size_t operator()(const DeformationKey& key) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        hashValue(hash, key.model);
        hashValue(hash, key.part);
        hashValue(hash, key.pose_entity);
        hashValue(hash, key.pose_revision);
        return static_cast<std::size_t>(hash);
    }
};

using DeformationCache = std::unordered_map<
    DeformationKey,
    Models::Runtime::DeformedPart,
    DeformationKeyHash>;

const std::vector<Models::Vertex>& vertices(const PreparedItem& prepared)
{
    return prepared.deformed ? prepared.deformed->vertices : prepared.item->mesh->vertices;
}

bool prepareItem(
    const Ecs::World& world,
    const Scene::RenderItem& item,
    const std::unordered_map<Models::MaterialHandle, std::uint32_t>& material_indices,
    DeformationCache& deformations,
    PreparedItem *prepared,
    std::string *error)
{
    if (!prepared) return false;
    *prepared = {};
    if (!item.mesh_component || !item.transform || !item.mesh) return true;
    if (item.mesh->indices.size() < 3u || item.mesh->vertices.empty()) return true;
    if (item.material && item.material->opacity < SceneCache::opacityCutoff()) return true;

    prepared->item = &item;
    if (const auto material = material_indices.find(item.mesh_component->material);
        material != material_indices.end())
        prepared->material_index = material->second;

    const Internal::ModelDeformComponent *deform = world.get<Internal::ModelDeformComponent>(item.entity);
    if (deform) {
        const Internal::ModelPoseComponent *pose = world.get<Internal::ModelPoseComponent>(deform->pose_entity);
        if (!pose) {
            if (error) *error = "dynamic model geometry has no pose state";
            return false;
        }
        const DeformationKey key{
            deform->model,
            deform->part,
            deform->pose_entity,
            pose->revision,
        };
        auto [deformed, inserted] = deformations.try_emplace(key);
        if (inserted && !Models::Runtime::deformPart(
                deform->model,
                deform->part,
                pose->pose,
                &deformed->second,
                error)) {
            deformations.erase(deformed);
            return false;
        }
        prepared->deformed = &deformed->second;
    }

    const auto& source_vertices = vertices(*prepared);
    const Math::Mat4 model = modelMatrix(*item.transform);
    const Math::Mat4 world_to_object = inverseModelMatrix(*item.transform);

    const Animation::Pose *pose = nullptr;
    if (!prepared->deformed) {
        const Animation::SkinBindingComponent *binding =
            world.get<Animation::SkinBindingComponent>(item.entity);
        if (binding && binding->animator != Ecs::INVALID_ENTITY) {
            const Animation::AnimatorComponent *animator =
                world.get<Animation::AnimatorComponent>(binding->animator);
            if (animator && !animator->pose.skin.empty()) pose = &animator->pose;
        }
    }

    prepared->positions.resize(source_vertices.size());
    prepared->normals.resize(source_vertices.size());
    for (std::size_t index = 0u; index < source_vertices.size(); ++index) {
        const Models::Vertex& vertex = source_vertices[index];
        Vec3 local_position{vertex.position.x, vertex.position.y, vertex.position.z};
        Vec3 local_normal{vertex.normal.x, vertex.normal.y, vertex.normal.z};

        if (pose) {
            Animation::Vec3 skinned_position{};
            Animation::Vec3 skinned_normal{};
            Animation::skinVertex(
                *pose,
                vertex.skin,
                {local_position.x, local_position.y, local_position.z},
                {local_normal.x, local_normal.y, local_normal.z},
                &skinned_position,
                &skinned_normal
            );
            local_position = {skinned_position.x, skinned_position.y, skinned_position.z};
            local_normal = {skinned_normal.x, skinned_normal.y, skinned_normal.z};
        }

        prepared->positions[index] = transformPoint(model, local_position);
        prepared->normals[index] = transformNormal(world_to_object, local_normal);
    }
    prepared->valid = true;
    return true;
}

bool triangleFor(
    const PreparedItem& prepared,
    std::uint32_t triangle_index,
    GpuTriangle *triangle)
{
    if (!triangle || !prepared.valid || !prepared.item || !prepared.item->mesh) return false;
    const Models::MeshData& mesh = *prepared.item->mesh;
    const auto& source_vertices = vertices(prepared);
    const std::size_t offset = static_cast<std::size_t>(triangle_index) * 3u;
    if (offset + 2u >= mesh.indices.size()) return false;
    const std::uint32_t i0 = mesh.indices[offset + 0u];
    const std::uint32_t i1 = mesh.indices[offset + 1u];
    const std::uint32_t i2 = mesh.indices[offset + 2u];
    if (i0 >= source_vertices.size() || i1 >= source_vertices.size() || i2 >= source_vertices.size())
        return false;

    const Models::Vertex& v0 = source_vertices[i0];
    const Models::Vertex& v1 = source_vertices[i1];
    const Models::Vertex& v2 = source_vertices[i2];
    const Vec3& p0 = prepared.positions[i0];
    const Vec3& p1 = prepared.positions[i1];
    const Vec3& p2 = prepared.positions[i2];
    const Vec3& n0 = prepared.normals[i0];
    const Vec3& n1 = prepared.normals[i1];
    const Vec3& n2 = prepared.normals[i2];

    triangle->p0 = {p0.x, p0.y, p0.z, std::bit_cast<float>(prepared.material_index)};
    triangle->p1 = {p1.x, p1.y, p1.z, std::bit_cast<float>(prepared.item->entity)};
    triangle->p2 = {p2.x, p2.y, p2.z, 0.0f};
    triangle->n0 = {n0.x, n0.y, n0.z, 0.0f};
    triangle->n1 = {n1.x, n1.y, n1.z, 0.0f};
    triangle->n2 = {n2.x, n2.y, n2.z, 0.0f};
    triangle->uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
    triangle->uv2 = {v2.uv.x, v2.uv.y, 0.0f, 0.0f};
    return true;
}

bool textureHasTransparency(Models::TextureHandle handle)
{
    const std::uint8_t threshold = SceneCache::alphaThreshold();
    if (threshold == 0u) return false;

    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset || asset->image.rgba.size() < 4u) return false;
    for (std::size_t index = 3u; index < asset->image.rgba.size(); index += 4u) {
        if (asset->image.rgba[index] < threshold) return true;
    }
    return false;
}

std::uint32_t unorm8(float value)
{
    return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

std::uint32_t packedTextureSlot(int slot)
{
    return slot >= 0 && slot < static_cast<int>(PackedTextureNone)
        ? static_cast<std::uint32_t>(slot)
        : PackedTextureNone;
}

std::uint32_t packTextureSlots(const std::array<int, 6>& slots)
{
    std::uint32_t result = 0u;
    for (std::size_t index = 0u; index < slots.size(); ++index)
        result |= packedTextureSlot(slots[index]) << static_cast<std::uint32_t>(index * 5u);
    return result;
}

std::uint32_t packMaterialParameters(const Models::MaterialData& material)
{
    return
        (unorm8(material.roughness) << 0u) |
        (unorm8(material.metallic) << 8u) |
        (unorm8(material.ambient_occlusion) << 16u) |
        (unorm8(material.clearcoat) << 24u);
}

std::uint32_t packEmissive(const Models::MaterialData& material)
{
    const std::uint32_t strength = unorm8(
        std::clamp(material.emissive_strength, 0.0f, MaximumPackedEmissiveStrength) /
        MaximumPackedEmissiveStrength
    );
    return
        (unorm8(material.emissive_color.x) << 0u) |
        (unorm8(material.emissive_color.y) << 8u) |
        (unorm8(material.emissive_color.z) << 16u) |
        (strength << 24u);
}

void appendMaterialTextures(
    const Models::MaterialData& material,
    std::vector<Models::TextureHandle>& out,
    std::unordered_set<Models::TextureHandle>& seen)
{
    const std::array<Models::TextureHandle, 7> handles {{
        material.diffuse_texture,
        material.normal_texture,
        material.roughness_texture,
        material.metallic_texture,
        material.ambient_occlusion_texture,
        material.emissive_texture,
        material.opacity_texture,
    }};

    for (const Models::TextureHandle handle : handles) {
        if (handle == Models::INVALID_TEXTURE || !seen.insert(handle).second) continue;
        out.push_back(handle);
    }
}

} // namespace

std::uint64_t SceneCache::signature(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items) const
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, config_revision_);
    for (const Scene::RenderItem& item : items) {
        if (!item.mesh_component || !item.transform) continue;
        hashValue(hash, item.entity);
        hashValue(hash, item.instance_index);
        hashValue(hash, item.mesh_component->mesh);
        hashValue(hash, Models::Internal::meshRevision(item.mesh_component->mesh));
        hashValue(hash, item.mesh_component->material);
        hashTransform(hash, *item.transform);

        const Internal::ModelDeformComponent *deform = world.get<Internal::ModelDeformComponent>(item.entity);
        if (deform && deform->pose_entity != Ecs::INVALID_ENTITY) {
            hashValue(hash, deform->model);
            hashValue(hash, deform->part);
            hashValue(hash, deform->pose_entity);
            if (const Internal::ModelPoseComponent *pose = world.get<Internal::ModelPoseComponent>(deform->pose_entity))
                hashValue(hash, pose->revision);
        }

        const Animation::SkinBindingComponent *binding =
            world.get<Animation::SkinBindingComponent>(item.entity);
        if (binding && binding->animator != Ecs::INVALID_ENTITY) {
            const Animation::AnimatorComponent *animator =
                world.get<Animation::AnimatorComponent>(binding->animator);
            if (animator) {
                hashValue(hash, binding->animator);
                hashValue(hash, animator->pose.revision);
            }
        }
    }
    return hash;
}

std::uint64_t SceneCache::topologySignature(const std::vector<Scene::RenderItem>& items) const
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, config_revision_);
    for (const Scene::RenderItem& item : items) {
        if (!item.mesh_component || !item.mesh) continue;
        hashValue(hash, item.entity);
        hashValue(hash, item.instance_index);
        hashValue(hash, item.mesh_component->mesh);
        hashValue(hash, Models::Internal::meshTopologyRevision(item.mesh_component->mesh));
        hashValue(hash, item.material && item.material->opacity < opacity_cutoff_ ? 0u : 1u);
    }
    return hash;
}

std::uint64_t SceneCache::resourceSignature(
    const std::vector<Scene::RenderItem>& items) const
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, config_revision_);
    hashValue(hash, Models::resourceRevision());
    for (const Scene::RenderItem& item : items) {
        if (!item.mesh_component) continue;
        hashValue(hash, item.mesh_component->material);
    }
    return hash;
}

std::uint32_t SceneCache::buildNode(
    std::uint32_t start,
    std::uint32_t count,
    std::vector<std::uint32_t>& order)
{
    const float infinity = std::numeric_limits<float>::infinity();
    Vec3 bounds_min{infinity, infinity, infinity};
    Vec3 bounds_max{-infinity, -infinity, -infinity};
    Vec3 centroid_min{infinity, infinity, infinity};
    Vec3 centroid_max{-infinity, -infinity, -infinity};

    for (std::uint32_t index = 0u; index < count; ++index) {
        const GpuTriangle& triangle = triangles_[order[start + index]];
        const Vec3 p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
        const Vec3 p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
        const Vec3 p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
        bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
        bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
        const Vec3 centroid = triangleCentroid(triangle);
        centroid_min = minVec(centroid_min, centroid);
        centroid_max = maxVec(centroid_max, centroid);
    }

    const std::uint32_t node_index = static_cast<std::uint32_t>(nodes_.size());
    GpuNode node;
    node.min_x = bounds_min.x;
    node.min_y = bounds_min.y;
    node.min_z = bounds_min.z;
    node.max_x = bounds_max.x;
    node.max_y = bounds_max.y;
    node.max_z = bounds_max.z;
    nodes_.push_back(node);

    const Vec3 extent = subtract(centroid_max, centroid_min);
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > component(extent, axis)) axis = 2;

    if (count <= std::max(leaf_size_, 1u) || component(extent, axis) <= 1.0e-6f) {
        nodes_[node_index].first = start;
        nodes_[node_index].meta = LeafBit | count;
        nodes_[node_index].extra[0] = static_cast<std::uint32_t>(nodes_.size());
        return node_index;
    }

    const std::uint32_t left_count = count / 2u;
    const std::uint32_t middle = start + left_count;
    std::nth_element(
        order.begin() + start,
        order.begin() + middle,
        order.begin() + start + count,
        [&](std::uint32_t a, std::uint32_t b) {
            return component(triangleCentroid(triangles_[a]), axis) <
                component(triangleCentroid(triangles_[b]), axis);
        }
    );

    const std::uint32_t left = buildNode(start, left_count, order);
    const std::uint32_t right = buildNode(middle, count - left_count, order);
    nodes_[node_index].first = left;
    nodes_[node_index].meta = right;
    nodes_[node_index].extra[0] = static_cast<std::uint32_t>(nodes_.size());
    return node_index;
}

void SceneCache::refitNodes()
{
    for (std::size_t index = nodes_.size(); index-- > 0u;) {
        GpuNode& node = nodes_[index];
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3 bounds_min{infinity, infinity, infinity};
        Vec3 bounds_max{-infinity, -infinity, -infinity};

        if ((node.meta & LeafBit) != 0u) {
            const std::uint32_t count = node.meta & ~LeafBit;
            for (std::uint32_t triangle_index = 0u; triangle_index < count; ++triangle_index) {
                const GpuTriangle& triangle = triangles_[node.first + triangle_index];
                const Vec3 p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
                const Vec3 p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
                const Vec3 p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
                bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
                bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
            }
        } else {
            const GpuNode& left = nodes_[node.first];
            const GpuNode& right = nodes_[node.meta];
            bounds_min = minVec(
                {left.min_x, left.min_y, left.min_z},
                {right.min_x, right.min_y, right.min_z});
            bounds_max = maxVec(
                {left.max_x, left.max_y, left.max_z},
                {right.max_x, right.max_y, right.max_z});
        }

        node.min_x = bounds_min.x;
        node.min_y = bounds_min.y;
        node.min_z = bounds_min.z;
        node.max_x = bounds_max.x;
        node.max_y = bounds_max.y;
        node.max_z = bounds_max.z;
    }
}

bool SceneCache::sync(
    const Ecs::World& world,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    Scene::collectRenderItems(world, render_items_);
    return sync(world, render_items_, maximum_texture_slots, error);
}

bool SceneCache::sync(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (maximum_triangles_ == 0u) {
        if (error) *error = "SceneCache maximum triangle count was not configured";
        return false;
    }
    if (!syncResources(world, items, maximum_texture_slots, error)) return false;

    const std::uint64_t current_topology_signature = topologySignature(items);
    const std::uint64_t current_geometry_signature = signature(world, items);
    if (!topology_initialized_ || current_topology_signature != topology_signature_) {
        if (!rebuildGeometry(world, items, error)) {
            clear();
            return false;
        }
        topology_signature_ = current_topology_signature;
        topology_initialized_ = true;
        ++topology_revision_;
        ++topology_updates_;
        geometry_signature_ = current_geometry_signature;
        geometry_initialized_ = true;
        ++geometry_revision_;
        ++geometry_updates_;
    } else if (!geometry_initialized_ || current_geometry_signature != geometry_signature_) {
        if (!updateGeometry(world, items, error)) {
            clear();
            return false;
        }
        geometry_signature_ = current_geometry_signature;
        geometry_initialized_ = true;
        ++geometry_revision_;
        ++geometry_updates_;
    }

    return true;
}

bool SceneCache::syncResources(
    const Ecs::World& world,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    Scene::collectRenderItems(world, render_items_);
    return syncResources(world, render_items_, maximum_texture_slots, error);
}

bool SceneCache::syncResources(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (error) error->clear();
    render_items_ = items;

    const EnvironmentState environment = environmentState(world);
    environment_texture_ = environment.valid ? environment.texture : Models::INVALID_TEXTURE;

    std::uint64_t current_resource_signature = resourceSignature(items);
    hashValue(current_resource_signature, environmentSignature(environment));
    if (!resources_initialized_ || current_resource_signature != resource_signature_) {
        if (!rebuildResources(items, maximum_texture_slots, error)) {
            clearResources();
            render_items_.clear();
            return false;
        }
        resource_signature_ = current_resource_signature;
        resources_initialized_ = true;
        ++resource_revision_;
        ++resource_updates_;
    }
    return true;
}

std::uint32_t SceneCache::materialIndex(Models::MaterialHandle handle) const
{
    const auto found = material_indices_.find(handle);
    return found == material_indices_.end() ? 0u : found->second;
}

bool SceneCache::rebuildResources(
    const std::vector<Scene::RenderItem>& items,
    std::size_t maximum_texture_slots,
    std::string *error)
{
    if (error) error->clear();
    materials_.clear();
    texture_handles_.clear();
    material_indices_.clear();

    std::vector<Models::TextureHandle> requested_textures;
    std::unordered_set<Models::TextureHandle> seen_textures;
    std::unordered_set<Models::TextureHandle> preferred_textures;
    std::vector<Models::MaterialHandle> requested_materials;
    std::unordered_set<Models::MaterialHandle> seen_materials;

    if (environment_texture_ != Models::INVALID_TEXTURE) {
        requested_textures.push_back(environment_texture_);
        seen_textures.insert(environment_texture_);
    }

    for (const Scene::RenderItem& item : items) {
        if (!item.mesh_component || !item.material || item.material->opacity < opacity_cutoff_) continue;
        appendMaterialTextures(*item.material, requested_textures, seen_textures);
        if (item.material->diffuse_texture != Models::INVALID_TEXTURE)
            preferred_textures.insert(item.material->diffuse_texture);
        if (item.material->opacity_texture != Models::INVALID_TEXTURE)
            preferred_textures.insert(item.material->opacity_texture);
        const Models::MaterialHandle handle = item.mesh_component->material;
        if (handle != Models::INVALID_MATERIAL && seen_materials.insert(handle).second)
            requested_materials.push_back(handle);
    }

    auto material_texture_begin = requested_textures.begin();
    if (environment_texture_ != Models::INVALID_TEXTURE && material_texture_begin != requested_textures.end())
        ++material_texture_begin;
    std::stable_sort(
        material_texture_begin,
        requested_textures.end(),
        [&](Models::TextureHandle a, Models::TextureHandle b) {
            const bool preferred_a = preferred_textures.find(a) != preferred_textures.end();
            const bool preferred_b = preferred_textures.find(b) != preferred_textures.end();
            if (preferred_a != preferred_b) return preferred_a && !preferred_b;
            const bool alpha_a = textureHasTransparency(a);
            const bool alpha_b = textureHasTransparency(b);
            if (alpha_a != alpha_b) return alpha_a && !alpha_b;
            return a < b;
        }
    );

    const std::size_t packed_limit = std::min<std::size_t>(maximum_texture_slots, PackedTextureNone);
    if (requested_textures.size() > packed_limit)
        requested_textures.resize(packed_limit);

    texture_handles_ = requested_textures;
    std::unordered_map<Models::TextureHandle, int> texture_indices;
    texture_indices.reserve(texture_handles_.size());
    for (std::size_t index = 0u; index < texture_handles_.size(); ++index)
        texture_indices.emplace(texture_handles_[index], static_cast<int>(index));

    auto textureSlot = [&](Models::TextureHandle handle) -> int {
        if (handle == Models::INVALID_TEXTURE) return -1;
        const auto found = texture_indices.find(handle);
        return found == texture_indices.end() ? -1 : found->second;
    };

    std::sort(requested_materials.begin(), requested_materials.end());
    materials_.push_back(GpuMaterial{});
    material_indices_.reserve(requested_materials.size());

    for (const Models::MaterialHandle handle : requested_materials) {
        const Models::MaterialData *material = Models::material(handle);
        if (!material) continue;

        GpuMaterial gpu_material;
        gpu_material.base_color = {
            material->color.x,
            material->color.y,
            material->color.z,
            std::clamp(material->opacity, 0.0f, 1.0f),
        };
        gpu_material.data[0] = textureSlot(material->diffuse_texture);
        const std::array<int, 6> slots {{
            textureSlot(material->normal_texture),
            textureSlot(material->roughness_texture),
            textureSlot(material->metallic_texture),
            textureSlot(material->ambient_occlusion_texture),
            textureSlot(material->emissive_texture),
            textureSlot(material->opacity_texture),
        }};
        gpu_material.data[1] = std::bit_cast<std::int32_t>(packTextureSlots(slots));
        gpu_material.data[2] = std::bit_cast<std::int32_t>(packMaterialParameters(*material));
        gpu_material.data[3] = std::bit_cast<std::int32_t>(packEmissive(*material));

        const std::uint32_t index = static_cast<std::uint32_t>(materials_.size());
        materials_.push_back(gpu_material);
        material_indices_.emplace(handle, index);
    }

    return true;
}

bool SceneCache::rebuildGeometry(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::string *error)
{
    nodes_.clear();
    triangles_.clear();
    triangle_sources_.clear();
    DeformationCache deformations;

    for (std::size_t item_index = 0u; item_index < items.size(); ++item_index) {
        if (triangles_.size() >= maximum_triangles_) break;
        PreparedItem prepared;
        if (!prepareItem(
                world,
                items[item_index],
                material_indices_,
                deformations,
                &prepared,
                error)) return false;
        if (!prepared.valid) continue;

        const std::size_t triangle_count = prepared.item->mesh->indices.size() / 3u;
        for (std::size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
            if (triangles_.size() >= maximum_triangles_) break;
            GpuTriangle triangle;
            if (!triangleFor(prepared, static_cast<std::uint32_t>(triangle_index), &triangle)) continue;
            triangles_.push_back(triangle);
            triangle_sources_.push_back(TriangleSource{
                static_cast<std::uint32_t>(item_index),
                static_cast<std::uint32_t>(triangle_index),
            });
        }
    }

    if (!triangles_.empty()) {
        if (nodes_.capacity() < triangles_.size() * 2u)
            nodes_.reserve(triangles_.size() * 2u);
        std::vector<std::uint32_t> order(triangles_.size());
        for (std::size_t index = 0u; index < order.size(); ++index)
            order[index] = static_cast<std::uint32_t>(index);
        buildNode(0u, static_cast<std::uint32_t>(triangles_.size()), order);

        std::vector<GpuTriangle> unsorted_triangles = std::move(triangles_);
        std::vector<TriangleSource> unsorted_sources = std::move(triangle_sources_);
        triangles_.resize(order.size());
        triangle_sources_.resize(order.size());
        for (std::size_t index = 0u; index < order.size(); ++index) {
            triangles_[index] = std::move(unsorted_triangles[order[index]]);
            triangle_sources_[index] = unsorted_sources[order[index]];
        }
    }
    return true;
}

bool SceneCache::updateGeometry(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items,
    std::string *error)
{
    if (triangles_.size() != triangle_sources_.size()) {
        if (error) *error = "scene geometry source mapping is inconsistent";
        return false;
    }

    std::vector<std::uint8_t> needed(items.size(), 0u);
    for (const TriangleSource& source : triangle_sources_) {
        if (source.item >= items.size()) {
            if (error) *error = "scene geometry source item is invalid";
            return false;
        }
        needed[source.item] = 1u;
    }

    DeformationCache deformations;
    std::vector<PreparedItem> prepared(items.size());
    for (std::size_t item_index = 0u; item_index < items.size(); ++item_index) {
        if (needed[item_index] == 0u) continue;
        if (!prepareItem(
                world,
                items[item_index],
                material_indices_,
                deformations,
                &prepared[item_index],
                error))
            return false;
        if (!prepared[item_index].valid) {
            if (error) *error = "scene geometry topology changed during incremental update";
            return false;
        }
    }

    for (std::size_t index = 0u; index < triangle_sources_.size(); ++index) {
        const TriangleSource& source = triangle_sources_[index];
        if (!triangleFor(prepared[source.item], source.triangle, &triangles_[index])) {
            if (error) *error = "scene geometry triangle changed during incremental update";
            return false;
        }
    }

    refitNodes();
    return true;
}

void SceneCache::clearGeometry()
{
    nodes_.clear();
    triangles_.clear();
    triangle_sources_.clear();
    geometry_initialized_ = false;
    topology_initialized_ = false;
}

void SceneCache::clearResources()
{
    materials_.clear();
    texture_handles_.clear();
    material_indices_.clear();
    environment_texture_ = Models::INVALID_TEXTURE;
    resources_initialized_ = false;
}

void SceneCache::clear()
{
    clearGeometry();
    clearResources();
    render_items_.clear();
    geometry_signature_ = std::numeric_limits<std::uint64_t>::max();
    topology_signature_ = std::numeric_limits<std::uint64_t>::max();
    resource_signature_ = std::numeric_limits<std::uint64_t>::max();
}

CameraState cameraState(const Scene::CameraState& source)
{
    CameraState state;
    if (!source.valid) return state;

    const Math::Mat4 model = Math::modelMatrix(source.transform);
    state.valid = true;
    state.position = Math::transformPoint(model, {0.0f, 0.0f, 0.0f});
    state.forward = normalize(Math::transformVector(model, {0.0f, 0.0f, -1.0f}));
    state.right = normalize(Math::transformVector(model, {1.0f, 0.0f, 0.0f}));
    state.up = normalize(Math::transformVector(model, {0.0f, 1.0f, 0.0f}));
    state.fov_degrees = std::clamp(source.fov_degrees, 1.0f, 179.0f);
    state.projection = source.projection;
    state.near_plane = source.near_plane;
    state.far_plane = source.far_plane;
    state.aspect_ratio = source.aspect_ratio;
    state.xmag = source.xmag;
    state.ymag = source.ymag;
    return state;
}

LightState lightState(const Scene::LightState& source)
{
    LightState state;
    if (!source.valid) return state;
    state.valid = true;
    state.type = source.light.type;
    state.position = source.transform.position;
    state.direction = normalize(Math::transformNormal(
        Math::modelMatrix(source.transform),
        {0.0f, 0.0f, -1.0f}
    ));
    state.color = source.light.color;
    state.intensity = std::max(source.light.intensity, 0.0f);
    state.range = std::max(source.light.range, 0.0f);
    state.inner_cone_degrees = std::clamp(source.light.inner_cone_degrees, 0.0f, 89.9f);
    state.outer_cone_degrees = std::clamp(
        std::max(source.light.outer_cone_degrees, state.inner_cone_degrees),
        state.inner_cone_degrees,
        89.9f
    );
    return state;
}

std::uint64_t cameraSignature(const CameraState& camera)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, camera.valid ? 1u : 0u);
    hashVec3(hash, camera.position);
    hashVec3(hash, camera.forward);
    hashVec3(hash, camera.right);
    hashVec3(hash, camera.up);
    hashFloat(hash, camera.fov_degrees);
    hashValue(hash, static_cast<std::uint32_t>(camera.projection));
    hashFloat(hash, camera.near_plane);
    hashFloat(hash, camera.far_plane);
    hashFloat(hash, camera.aspect_ratio);
    hashFloat(hash, camera.xmag);
    hashFloat(hash, camera.ymag);
    return hash;
}

std::uint64_t lightSignature(const LightState& light)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, light.valid ? 1u : 0u);
    hashValue(hash, static_cast<std::uint32_t>(light.type));
    hashVec3(hash, light.position);
    hashVec3(hash, light.direction);
    hashVec3(hash, light.color);
    hashFloat(hash, light.intensity);
    hashFloat(hash, light.range);
    hashFloat(hash, light.inner_cone_degrees);
    hashFloat(hash, light.outer_cone_degrees);
    return hash;
}

} // namespace Renderer::Scenes
