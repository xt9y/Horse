#include "Renderer/Scenes/SceneCache.hpp"

#include "Animation/Animation.hpp"
#include "Camera.hpp"
#include "Models/Core/Texture.hpp"
#include "Renderer/Math.hpp"

#include <algorithm>
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

bool textureHasTransparency(Models::TextureHandle handle)
{
    const Models::TextureAsset *asset = Models::texture(handle);
    if (!asset || asset->image.rgba.size() < 4u) return false;
    const std::uint8_t threshold = SceneCache::alphaThreshold();
    for (std::size_t index = 3u; index < asset->image.rgba.size(); index += 4u) {
        if (asset->image.rgba[index] < threshold) return true;
    }
    return false;
}

} // namespace

std::uint64_t SceneCache::signature(
    const Ecs::World& world,
    const std::vector<Scene::RenderItem>& items) const
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const Scene::RenderItem& item : items) {
        if (!item.mesh_component || !item.transform) continue;
        hashValue(hash, item.entity);
        hashValue(hash, item.mesh_component->mesh);
        hashValue(hash, item.mesh_component->material);
        hashTransform(hash, *item.transform);

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

std::uint32_t SceneCache::buildNode(std::uint32_t start, std::uint32_t count)
{
    const float infinity = std::numeric_limits<float>::infinity();
    Vec3 bounds_min{infinity, infinity, infinity};
    Vec3 bounds_max{-infinity, -infinity, -infinity};
    Vec3 centroid_min{infinity, infinity, infinity};
    Vec3 centroid_max{-infinity, -infinity, -infinity};

    for (std::uint32_t index = 0u; index < count; ++index) {
        const GpuTriangle& triangle = triangles_[start + index];
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
        triangles_.begin() + start,
        triangles_.begin() + middle,
        triangles_.begin() + start + count,
        [axis](const GpuTriangle& a, const GpuTriangle& b) {
            return component(triangleCentroid(a), axis) < component(triangleCentroid(b), axis);
        }
    );

    const std::uint32_t left = buildNode(start, left_count);
    const std::uint32_t right = buildNode(middle, count - left_count);
    nodes_[node_index].first = left;
    nodes_[node_index].meta = right;
    nodes_[node_index].extra[0] = static_cast<std::uint32_t>(nodes_.size());
    return node_index;
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
    clear();
    render_items_ = items;

    if (maximum_triangles_ == 0u) {
        if (error) *error = "SceneCache maximum triangle count was not configured";
        return false;
    }

    std::vector<Models::TextureHandle> requested_textures;
    std::unordered_set<Models::TextureHandle> seen_textures;
    for (const Scene::RenderItem& item : items) {
        if (!item.material || item.material->opacity < opacity_cutoff_) continue;
        const Models::TextureHandle handle = item.material->diffuse_texture;
        if (handle == Models::INVALID_TEXTURE) continue;
        if (seen_textures.insert(handle).second) requested_textures.push_back(handle);
    }

    std::stable_sort(
        requested_textures.begin(),
        requested_textures.end(),
        [](Models::TextureHandle a, Models::TextureHandle b) {
            return textureHasTransparency(a) && !textureHasTransparency(b);
        }
    );

    if (requested_textures.size() > maximum_texture_slots) {
        if (error) {
            *error = "ray scene requires " + std::to_string(requested_textures.size()) +
                " diffuse textures but backend supports " + std::to_string(maximum_texture_slots);
        }
        clear();
        return false;
    }

    texture_handles_ = requested_textures;
    std::unordered_map<Models::TextureHandle, int> texture_indices;
    for (std::size_t index = 0u; index < texture_handles_.size(); ++index) {
        texture_indices.emplace(texture_handles_[index], static_cast<int>(index));
    }

    std::unordered_map<Models::MaterialHandle, std::uint32_t> material_indices;
    auto materialIndex = [&](Models::MaterialHandle handle) -> std::uint32_t {
        const auto found = material_indices.find(handle);
        if (found != material_indices.end()) return found->second;

        GpuMaterial gpu_material;
        const Models::MaterialData *material = Models::material(handle);
        if (material) {
            gpu_material.base_color = {
                material->color.x,
                material->color.y,
                material->color.z,
                std::clamp(material->opacity, 0.0f, 1.0f),
            };
            if (material->diffuse_texture != Models::INVALID_TEXTURE) {
                const auto texture = texture_indices.find(material->diffuse_texture);
                if (texture != texture_indices.end()) gpu_material.data[0] = texture->second;
            }
        }

        const std::uint32_t index = static_cast<std::uint32_t>(materials_.size());
        materials_.push_back(gpu_material);
        material_indices.emplace(handle, index);
        return index;
    };

    for (const Scene::RenderItem& item : items) {
        if (triangles_.size() >= maximum_triangles_) break;
        if (!item.mesh_component || !item.transform || !item.mesh) continue;

        const Models::MeshData *mesh = item.mesh;
        if (mesh->indices.size() < 3u || mesh->vertices.empty()) continue;
        if (item.material && item.material->opacity < opacity_cutoff_) continue;

        const std::uint32_t material_index = materialIndex(item.mesh_component->material);
        const Math::Mat4 model = modelMatrix(*item.transform);
        const Math::Mat4 world_to_object = inverseModelMatrix(*item.transform);

        const Animation::Pose *pose = nullptr;
        const Animation::SkinBindingComponent *binding =
            world.get<Animation::SkinBindingComponent>(item.entity);
        if (binding && binding->animator != Ecs::INVALID_ENTITY) {
            const Animation::AnimatorComponent *animator =
                world.get<Animation::AnimatorComponent>(binding->animator);
            if (animator && !animator->pose.skin.empty()) pose = &animator->pose;
        }

        std::vector<Vec3> positions(mesh->vertices.size());
        std::vector<Vec3> normals(mesh->vertices.size());
        for (std::size_t index = 0u; index < mesh->vertices.size(); ++index) {
            const Models::Vertex& vertex = mesh->vertices[index];
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

            positions[index] = transformPoint(model, local_position);
            normals[index] = transformNormal(world_to_object, local_normal);
        }

        const std::size_t triangle_count = mesh->indices.size() / 3u;
        for (std::size_t triangle_index = 0u; triangle_index < triangle_count; ++triangle_index) {
            if (triangles_.size() >= maximum_triangles_) break;
            const std::size_t offset = triangle_index * 3u;
            const std::uint32_t i0 = mesh->indices[offset + 0u];
            const std::uint32_t i1 = mesh->indices[offset + 1u];
            const std::uint32_t i2 = mesh->indices[offset + 2u];
            if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;

            const Models::Vertex& v0 = mesh->vertices[i0];
            const Models::Vertex& v1 = mesh->vertices[i1];
            const Models::Vertex& v2 = mesh->vertices[i2];
            const Vec3& p0 = positions[i0];
            const Vec3& p1 = positions[i1];
            const Vec3& p2 = positions[i2];
            const Vec3& n0 = normals[i0];
            const Vec3& n1 = normals[i1];
            const Vec3& n2 = normals[i2];

            GpuTriangle triangle;
            triangle.p0 = {p0.x, p0.y, p0.z, std::bit_cast<float>(material_index)};
            triangle.p1 = {p1.x, p1.y, p1.z, std::bit_cast<float>(item.entity)};
            triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
            triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
            triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
            triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
            triangle.uv01 = {v0.uv.x, v0.uv.y, v1.uv.x, v1.uv.y};
            triangle.uv2 = {v2.uv.x, v2.uv.y, 0.0f, 0.0f};
            triangles_.push_back(triangle);
        }
    }

    if (materials_.empty()) materials_.push_back(GpuMaterial{});
    if (!triangles_.empty()) {
        nodes_.reserve(triangles_.size() * 2u);
        buildNode(0u, static_cast<std::uint32_t>(triangles_.size()));
    }
    return true;
}

void SceneCache::clear()
{
    nodes_.clear();
    triangles_.clear();
    materials_.clear();
    texture_handles_.clear();
    render_items_.clear();
}

CameraState cameraState(const Scene::CameraState& source)
{
    CameraState state;
    if (!source.valid) return state;

    const Vec3 forward = Camera::flightDirection(
        source.transform.rotation.y,
        source.transform.rotation.x
    );
    const Vec3 right = Camera::strafeDirection(source.transform.rotation.y);
    state.valid = true;
    state.position = source.transform.position;
    state.forward = normalize(forward);
    state.right = normalize(right);
    state.up = normalize(Math::cross(state.right, state.forward));
    state.fov_degrees = std::clamp(source.fov_degrees, 1.0f, 179.0f);
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
    return state;
}

std::uint64_t cameraSignature(const CameraState& camera)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, camera.valid ? 1u : 0u);
    hashVec3(hash, camera.position);
    hashVec3(hash, camera.forward);
    hashFloat(hash, camera.fov_degrees);
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
    return hash;
}

} // namespace Renderer::Scenes
