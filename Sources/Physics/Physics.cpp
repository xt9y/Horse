#include "Physics/Physics.hpp"

#include "Renderer/Hierarchy.hpp"
#include "Renderer/Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Physics {
namespace {

using Renderer::Math::Mat4;
using Renderer::Vec3;

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 multiply(Vec3 value, float scalar) { return {value.x * scalar, value.y * scalar, value.z * scalar}; }
float dot(Vec3 a, Vec3 b) { return Renderer::Math::dot(a, b); }
float lengthSquared(Vec3 value) { return dot(value, value); }
float length(Vec3 value) { return std::sqrt(lengthSquared(value)); }
Vec3 normalize(Vec3 value) { return Renderer::Math::normalize(value); }

Vec3 minVec(Vec3 a, Vec3 b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3 maxVec(Vec3 a, Vec3 b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

float component(Vec3 value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

float matrixScale(const Mat4& matrix)
{
    const float x = length({matrix[0], matrix[1], matrix[2]});
    const float y = length({matrix[4], matrix[5], matrix[6]});
    const float z = length({matrix[8], matrix[9], matrix[10]});
    return std::max({x, y, z});
}

Vec3 matrixAxisScale(const Mat4& matrix)
{
    return {
        length({matrix[0], matrix[1], matrix[2]}),
        length({matrix[4], matrix[5], matrix[6]}),
        length({matrix[8], matrix[9], matrix[10]}),
    };
}

bool entityMatrix(const Ecs::World& world, Ecs::Entity entity, Mat4 *matrix)
{
    return matrix && Renderer::Hierarchy::worldMatrix(world, entity, matrix);
}

struct LocalHit {
    float distance = std::numeric_limits<float>::max();
    Vec3 normal{};
};

bool raySphere(
    Vec3 origin,
    Vec3 direction,
    Vec3 center,
    float radius,
    float maximum_distance,
    LocalHit *hit)
{
    radius = std::max(radius, 0.0f);
    const Vec3 offset = subtract(origin, center);
    const float b = dot(offset, direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;

    const float root = std::sqrt(discriminant);
    float distance = -b - root;
    if (distance < 0.0f) distance = -b + root;
    if (distance < 0.0f || distance > maximum_distance) return false;

    if (hit) {
        hit->distance = distance;
        hit->normal = normalize(subtract(add(origin, multiply(direction, distance)), center));
    }
    return true;
}

bool rayBox(
    Vec3 origin,
    Vec3 direction,
    const Mat4& model,
    const BoxCollider& box,
    Vec3 padding,
    float maximum_distance,
    LocalHit *hit)
{
    Mat4 inverse{};
    if (!Renderer::Math::inverseMatrix(model, &inverse)) return false;

    const Vec3 local_origin = Renderer::Math::transformPoint(inverse, origin);
    const Vec3 local_direction = Renderer::Math::transformVector(inverse, direction);
    const Vec3 half {
        std::max(box.half_extents.x, 0.0f) + std::max(padding.x, 0.0f),
        std::max(box.half_extents.y, 0.0f) + std::max(padding.y, 0.0f),
        std::max(box.half_extents.z, 0.0f) + std::max(padding.z, 0.0f),
    };
    const Vec3 minimum {box.center.x - half.x, box.center.y - half.y, box.center.z - half.z};
    const Vec3 maximum {box.center.x + half.x, box.center.y + half.y, box.center.z + half.z};

    float near_distance = 0.0f;
    float far_distance = maximum_distance;
    Vec3 near_normal{};
    Vec3 far_normal{};

    const float origin_axis[3] = {local_origin.x, local_origin.y, local_origin.z};
    const float direction_axis[3] = {local_direction.x, local_direction.y, local_direction.z};
    const float minimum_axis[3] = {minimum.x, minimum.y, minimum.z};
    const float maximum_axis[3] = {maximum.x, maximum.y, maximum.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction_axis[axis]) <= 1.0e-10f) {
            if (origin_axis[axis] < minimum_axis[axis] || origin_axis[axis] > maximum_axis[axis])
                return false;
            continue;
        }

        float first = (minimum_axis[axis] - origin_axis[axis]) / direction_axis[axis];
        float second = (maximum_axis[axis] - origin_axis[axis]) / direction_axis[axis];
        float first_sign = -1.0f;
        float second_sign = 1.0f;
        if (first > second) {
            std::swap(first, second);
            std::swap(first_sign, second_sign);
        }

        if (first > near_distance) {
            near_distance = first;
            near_normal = {};
            if (axis == 0) near_normal.x = first_sign;
            if (axis == 1) near_normal.y = first_sign;
            if (axis == 2) near_normal.z = first_sign;
        }
        if (second < far_distance) {
            far_distance = second;
            far_normal = {};
            if (axis == 0) far_normal.x = second_sign;
            if (axis == 1) far_normal.y = second_sign;
            if (axis == 2) far_normal.z = second_sign;
        }
        if (near_distance > far_distance) return false;
    }

    float distance = near_distance;
    Vec3 normal = near_normal;
    if (distance <= 1.0e-6f) {
        distance = far_distance;
        normal = far_normal;
    }
    if (distance < 0.0f || distance > maximum_distance) return false;

    if (hit) {
        hit->distance = distance;
        hit->normal = Renderer::Math::transformNormal(inverse, normal);
    }
    return true;
}

bool rayBox(
    Vec3 origin,
    Vec3 direction,
    const Mat4& model,
    const BoxCollider& box,
    float maximum_distance,
    LocalHit *hit)
{
    return rayBox(origin, direction, model, box, {}, maximum_distance, hit);
}

float closestSegmentParameter(Vec3 point, Vec3 a, Vec3 b)
{
    const Vec3 segment = subtract(b, a);
    const float denominator = lengthSquared(segment);
    if (denominator <= 1.0e-20f) return 0.0f;
    return std::clamp(dot(subtract(point, a), segment) / denominator, 0.0f, 1.0f);
}

bool rayCapsule(
    Vec3 origin,
    Vec3 direction,
    Vec3 a,
    Vec3 b,
    float radius,
    float maximum_distance,
    LocalHit *hit)
{
    const Vec3 ba = subtract(b, a);
    const Vec3 oa = subtract(origin, a);
    const float baba = dot(ba, ba);
    const float bard = dot(ba, direction);
    const float baoa = dot(ba, oa);
    const float rdoa = dot(direction, oa);
    const float oaoa = dot(oa, oa);
    const float radius_squared = radius * radius;

    LocalHit best{};
    bool found = false;

    const float qa = baba - bard * bard;
    const float qb = baba * rdoa - baoa * bard;
    const float qc = baba * oaoa - baoa * baoa - radius_squared * baba;
    const float discriminant = qb * qb - qa * qc;
    if (std::abs(qa) > 1.0e-10f && discriminant >= 0.0f) {
        const float distance = (-qb - std::sqrt(discriminant)) / qa;
        const float y = baoa + distance * bard;
        if (distance >= 0.0f && distance <= maximum_distance && y > 0.0f && y < baba) {
            const Vec3 position = add(origin, multiply(direction, distance));
            const Vec3 axis_point = add(a, multiply(ba, y / baba));
            best.distance = distance;
            best.normal = normalize(subtract(position, axis_point));
            found = true;
        }
    }

    LocalHit cap{};
    if (raySphere(origin, direction, a, radius, maximum_distance, &cap) &&
        (!found || cap.distance < best.distance))
    {
        best = cap;
        found = true;
    }
    if (raySphere(origin, direction, b, radius, maximum_distance, &cap) &&
        (!found || cap.distance < best.distance))
    {
        best = cap;
        found = true;
    }

    if (found && hit) *hit = best;
    return found;
}

bool colliderRaycast(
    const Ecs::World& world,
    Ecs::Entity entity,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    LocalHit *hit)
{
    Mat4 model{};
    if (!entityMatrix(world, entity, &model)) return false;

    LocalHit best{};
    bool found = false;

    if (const BoxCollider *box = world.get<BoxCollider>(entity)) {
        LocalHit candidate{};
        if (rayBox(origin, direction, model, *box, maximum_distance, &candidate)) {
            best = candidate;
            found = true;
        }
    }

    if (const SphereCollider *sphere = world.get<SphereCollider>(entity)) {
        const Vec3 center = Renderer::Math::transformPoint(model, sphere->center);
        const float radius = std::max(sphere->radius, 0.0f) * matrixScale(model);
        LocalHit candidate{};
        if (raySphere(origin, direction, center, radius, maximum_distance, &candidate) &&
            (!found || candidate.distance < best.distance))
        {
            best = candidate;
            found = true;
        }
    }

    if (const CapsuleCollider *capsule = world.get<CapsuleCollider>(entity)) {
        const float half_height = std::max(capsule->half_height, 0.0f);
        const Vec3 local_a {capsule->center.x, capsule->center.y - half_height, capsule->center.z};
        const Vec3 local_b {capsule->center.x, capsule->center.y + half_height, capsule->center.z};
        const Vec3 a = Renderer::Math::transformPoint(model, local_a);
        const Vec3 b = Renderer::Math::transformPoint(model, local_b);
        const float radius = std::max(capsule->radius, 0.0f) * matrixScale(model);
        LocalHit candidate{};
        if (rayCapsule(origin, direction, a, b, radius, maximum_distance, &candidate) &&
            (!found || candidate.distance < best.distance))
        {
            best = candidate;
            found = true;
        }
    }

    if (found && hit) *hit = best;
    return found;
}

void expand(Vec3 point, Vec3 *minimum, Vec3 *maximum)
{
    if (!minimum || !maximum) return;
    *minimum = minVec(*minimum, point);
    *maximum = maxVec(*maximum, point);
}

bool colliderBounds(
    const Ecs::World& world,
    Ecs::Entity entity,
    Vec3 *minimum,
    Vec3 *maximum)
{
    if (!minimum || !maximum) return false;
    Mat4 model{};
    if (!entityMatrix(world, entity, &model)) return false;

    const float large = std::numeric_limits<float>::max();
    Vec3 out_min {large, large, large};
    Vec3 out_max {-large, -large, -large};
    bool found = false;

    if (const BoxCollider *box = world.get<BoxCollider>(entity)) {
        const Vec3 half {
            std::max(box->half_extents.x, 0.0f),
            std::max(box->half_extents.y, 0.0f),
            std::max(box->half_extents.z, 0.0f),
        };
        for (int x = -1; x <= 1; x += 2) {
            for (int y = -1; y <= 1; y += 2) {
                for (int z = -1; z <= 1; z += 2) {
                    expand(Renderer::Math::transformPoint(model, {
                        box->center.x + half.x * static_cast<float>(x),
                        box->center.y + half.y * static_cast<float>(y),
                        box->center.z + half.z * static_cast<float>(z),
                    }), &out_min, &out_max);
                }
            }
        }
        found = true;
    }

    if (const SphereCollider *sphere = world.get<SphereCollider>(entity)) {
        const Vec3 center = Renderer::Math::transformPoint(model, sphere->center);
        const float radius = std::max(sphere->radius, 0.0f) * matrixScale(model);
        expand({center.x - radius, center.y - radius, center.z - radius}, &out_min, &out_max);
        expand({center.x + radius, center.y + radius, center.z + radius}, &out_min, &out_max);
        found = true;
    }

    if (const CapsuleCollider *capsule = world.get<CapsuleCollider>(entity)) {
        const float half_height = std::max(capsule->half_height, 0.0f);
        const Vec3 a = Renderer::Math::transformPoint(model, {
            capsule->center.x, capsule->center.y - half_height, capsule->center.z,
        });
        const Vec3 b = Renderer::Math::transformPoint(model, {
            capsule->center.x, capsule->center.y + half_height, capsule->center.z,
        });
        const float radius = std::max(capsule->radius, 0.0f) * matrixScale(model);
        expand({a.x - radius, a.y - radius, a.z - radius}, &out_min, &out_max);
        expand({a.x + radius, a.y + radius, a.z + radius}, &out_min, &out_max);
        expand({b.x - radius, b.y - radius, b.z - radius}, &out_min, &out_max);
        expand({b.x + radius, b.y + radius, b.z + radius}, &out_min, &out_max);
        found = true;
    }

    if (!found) return false;
    *minimum = out_min;
    *maximum = out_max;
    return true;
}

bool boundsIntersect(Vec3 a_min, Vec3 a_max, Vec3 b_min, Vec3 b_max)
{
    return a_min.x <= b_max.x && a_max.x >= b_min.x &&
        a_min.y <= b_max.y && a_max.y >= b_min.y &&
        a_min.z <= b_max.z && a_max.z >= b_min.z;
}

bool sphereOverlapsEntity(
    const Ecs::World& world,
    Ecs::Entity entity,
    Vec3 center,
    float radius)
{
    Mat4 model{};
    if (!entityMatrix(world, entity, &model)) return false;
    const float radius_squared = radius * radius;

    if (const SphereCollider *sphere = world.get<SphereCollider>(entity)) {
        const Vec3 collider_center = Renderer::Math::transformPoint(model, sphere->center);
        const float collider_radius = std::max(sphere->radius, 0.0f) * matrixScale(model);
        const float total = radius + collider_radius;
        if (lengthSquared(subtract(center, collider_center)) <= total * total) return true;
    }

    if (const CapsuleCollider *capsule = world.get<CapsuleCollider>(entity)) {
        const float half_height = std::max(capsule->half_height, 0.0f);
        const Vec3 a = Renderer::Math::transformPoint(model, {
            capsule->center.x, capsule->center.y - half_height, capsule->center.z,
        });
        const Vec3 b = Renderer::Math::transformPoint(model, {
            capsule->center.x, capsule->center.y + half_height, capsule->center.z,
        });
        const float t = closestSegmentParameter(center, a, b);
        const Vec3 closest = add(a, multiply(subtract(b, a), t));
        const float total = radius + std::max(capsule->radius, 0.0f) * matrixScale(model);
        if (lengthSquared(subtract(center, closest)) <= total * total) return true;
    }

    if (const BoxCollider *box = world.get<BoxCollider>(entity)) {
        Mat4 inverse{};
        if (Renderer::Math::inverseMatrix(model, &inverse)) {
            const Vec3 local = Renderer::Math::transformPoint(inverse, center);
            const Vec3 half {
                std::max(box->half_extents.x, 0.0f),
                std::max(box->half_extents.y, 0.0f),
                std::max(box->half_extents.z, 0.0f),
            };
            const Vec3 closest_local {
                std::clamp(local.x, box->center.x - half.x, box->center.x + half.x),
                std::clamp(local.y, box->center.y - half.y, box->center.y + half.y),
                std::clamp(local.z, box->center.z - half.z, box->center.z + half.z),
            };
            const Vec3 closest_world = Renderer::Math::transformPoint(model, closest_local);
            if (lengthSquared(subtract(center, closest_world)) <= radius_squared) return true;
        }
    }

    return false;
}

bool colliderSphereCast(
    const Ecs::World& world,
    Ecs::Entity entity,
    Vec3 origin,
    float radius,
    Vec3 direction,
    float maximum_distance,
    LocalHit *hit)
{
    if (sphereOverlapsEntity(world, entity, origin, radius)) {
        if (hit) {
            hit->distance = 0.0f;
            hit->normal = multiply(direction, -1.0f);
        }
        return true;
    }

    Mat4 model{};
    if (!entityMatrix(world, entity, &model)) return false;

    LocalHit best{};
    bool found = false;

    if (const BoxCollider *box = world.get<BoxCollider>(entity)) {
        const Vec3 scale = matrixAxisScale(model);
        if (scale.x > 1.0e-8f && scale.y > 1.0e-8f && scale.z > 1.0e-8f) {
            const Vec3 padding {radius / scale.x, radius / scale.y, radius / scale.z};
            LocalHit candidate{};
            if (rayBox(origin, direction, model, *box, padding, maximum_distance, &candidate)) {
                best = candidate;
                found = true;
            }
        }
    }

    if (const SphereCollider *sphere = world.get<SphereCollider>(entity)) {
        const Vec3 center = Renderer::Math::transformPoint(model, sphere->center);
        const float collider_radius = std::max(sphere->radius, 0.0f) * matrixScale(model);
        LocalHit candidate{};
        if (raySphere(origin, direction, center, collider_radius + radius, maximum_distance, &candidate) &&
            (!found || candidate.distance < best.distance))
        {
            best = candidate;
            found = true;
        }
    }

    if (const CapsuleCollider *capsule = world.get<CapsuleCollider>(entity)) {
        const float half_height = std::max(capsule->half_height, 0.0f);
        const Vec3 a = Renderer::Math::transformPoint(model, {
            capsule->center.x, capsule->center.y - half_height, capsule->center.z,
        });
        const Vec3 b = Renderer::Math::transformPoint(model, {
            capsule->center.x, capsule->center.y + half_height, capsule->center.z,
        });
        const float collider_radius = std::max(capsule->radius, 0.0f) * matrixScale(model);
        LocalHit candidate{};
        if (rayCapsule(origin, direction, a, b, collider_radius + radius, maximum_distance, &candidate) &&
            (!found || candidate.distance < best.distance))
        {
            best = candidate;
            found = true;
        }
    }

    if (found && hit) *hit = best;
    return found;
}

struct BroadphaseEntry {
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Vec3 minimum{};
    Vec3 maximum{};
    Vec3 center{};
};

struct BroadphaseNode {
    Vec3 minimum{};
    Vec3 maximum{};
    std::uint32_t first = 0u;
    std::uint32_t count = 0u;
    std::uint32_t left = 0u;
    std::uint32_t right = 0u;
};

struct BroadphaseCache {
    const Ecs::World *world = nullptr;
    std::uint64_t structure_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t transform_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t resource_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t rebuilds = 0u;
    std::vector<BroadphaseEntry> entries;
    std::vector<BroadphaseNode> nodes;
};

BroadphaseCache& broadphase()
{
    static BroadphaseCache cache;
    return cache;
}

std::uint32_t buildBroadphaseNode(BroadphaseCache& cache, std::uint32_t first, std::uint32_t count)
{
    const float large = std::numeric_limits<float>::max();
    Vec3 minimum {large, large, large};
    Vec3 maximum {-large, -large, -large};
    Vec3 center_min {large, large, large};
    Vec3 center_max {-large, -large, -large};

    for (std::uint32_t i = 0u; i < count; ++i) {
        const BroadphaseEntry& entry = cache.entries[first + i];
        minimum = minVec(minimum, entry.minimum);
        maximum = maxVec(maximum, entry.maximum);
        center_min = minVec(center_min, entry.center);
        center_max = maxVec(center_max, entry.center);
    }

    const std::uint32_t node_index = static_cast<std::uint32_t>(cache.nodes.size());
    cache.nodes.push_back({minimum, maximum, first, count, 0u, 0u});
    if (count <= 4u) return node_index;

    const Vec3 extent = subtract(center_max, center_min);
    int axis = extent.y > extent.x ? 1 : 0;
    if (extent.z > component(extent, axis)) axis = 2;
    if (component(extent, axis) <= 1.0e-6f) return node_index;

    const std::uint32_t left_count = count / 2u;
    const std::uint32_t middle = first + left_count;
    std::nth_element(
        cache.entries.begin() + first,
        cache.entries.begin() + middle,
        cache.entries.begin() + first + count,
        [axis](const BroadphaseEntry& a, const BroadphaseEntry& b) {
            return component(a.center, axis) < component(b.center, axis);
        }
    );

    const std::uint32_t left = buildBroadphaseNode(cache, first, left_count);
    const std::uint32_t right = buildBroadphaseNode(cache, middle, count - left_count);
    cache.nodes[node_index].count = 0u;
    cache.nodes[node_index].left = left;
    cache.nodes[node_index].right = right;
    return node_index;
}

void syncBroadphase(const Ecs::World& world)
{
    BroadphaseCache& cache = broadphase();
    const std::uint64_t structure = world.changeRevision(Ecs::ChangeKind::Structure);
    const std::uint64_t transform = world.changeRevision(Ecs::ChangeKind::Transform);
    const std::uint64_t resource = world.changeRevision(Ecs::ChangeKind::Resource);
    if (cache.world == &world && cache.structure_revision == structure &&
        cache.transform_revision == transform && cache.resource_revision == resource)
    {
        return;
    }

    cache.world = &world;
    cache.structure_revision = structure;
    cache.transform_revision = transform;
    cache.resource_revision = resource;
    cache.entries.clear();
    cache.nodes.clear();
    cache.entries.reserve(world.size());

    for (const Ecs::Entity entity : world.entities()) {
        Vec3 minimum{};
        Vec3 maximum{};
        if (!colliderBounds(world, entity, &minimum, &maximum)) continue;
        cache.entries.push_back({
            entity,
            minimum,
            maximum,
            multiply(add(minimum, maximum), 0.5f),
        });
    }

    if (!cache.entries.empty()) {
        cache.nodes.reserve(cache.entries.size() * 2u);
        buildBroadphaseNode(cache, 0u, static_cast<std::uint32_t>(cache.entries.size()));
    }
    ++cache.rebuilds;
}

bool rayAabb(Vec3 origin, Vec3 direction, Vec3 minimum, Vec3 maximum, float maximum_distance)
{
    float near_distance = 0.0f;
    float far_distance = maximum_distance;
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {direction.x, direction.y, direction.z};
    const float mn[3] = {minimum.x, minimum.y, minimum.z};
    const float mx[3] = {maximum.x, maximum.y, maximum.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) <= 1.0e-10f) {
            if (o[axis] < mn[axis] || o[axis] > mx[axis]) return false;
            continue;
        }
        float first = (mn[axis] - o[axis]) / d[axis];
        float second = (mx[axis] - o[axis]) / d[axis];
        if (first > second) std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (near_distance > far_distance) return false;
    }
    return far_distance >= 0.0f && near_distance <= maximum_distance;
}

void queryAabbCandidates(Vec3 minimum, Vec3 maximum, std::vector<Ecs::Entity>& out)
{
    out.clear();
    BroadphaseCache& cache = broadphase();
    if (cache.nodes.empty()) return;

    std::vector<std::uint32_t> stack;
    stack.reserve(64u);
    stack.push_back(0u);
    while (!stack.empty()) {
        const std::uint32_t index = stack.back();
        stack.pop_back();
        if (index >= cache.nodes.size()) continue;
        const BroadphaseNode& node = cache.nodes[index];
        if (!boundsIntersect(minimum, maximum, node.minimum, node.maximum)) continue;

        if (node.count != 0u) {
            for (std::uint32_t i = 0u; i < node.count; ++i) {
                const BroadphaseEntry& entry = cache.entries[node.first + i];
                if (boundsIntersect(minimum, maximum, entry.minimum, entry.maximum))
                    out.push_back(entry.entity);
            }
        } else {
            stack.push_back(node.left);
            stack.push_back(node.right);
        }
    }
}

} // namespace

bool raycast(
    const Ecs::World& world,
    Vec3 origin,
    Vec3 direction,
    float maximum_distance,
    RaycastHit *hit)
{
    if (maximum_distance <= 0.0f || lengthSquared(direction) <= 1.0e-20f) return false;
    direction = normalize(direction);
    syncBroadphase(world);

    BroadphaseCache& cache = broadphase();
    if (cache.nodes.empty()) return false;

    bool found = false;
    RaycastHit best{};
    best.distance = maximum_distance;
    std::vector<std::uint32_t> stack;
    stack.reserve(64u);
    stack.push_back(0u);

    while (!stack.empty()) {
        const std::uint32_t index = stack.back();
        stack.pop_back();
        if (index >= cache.nodes.size()) continue;
        const BroadphaseNode& node = cache.nodes[index];
        if (!rayAabb(origin, direction, node.minimum, node.maximum, best.distance)) continue;

        if (node.count != 0u) {
            for (std::uint32_t i = 0u; i < node.count; ++i) {
                const BroadphaseEntry& entry = cache.entries[node.first + i];
                if (!rayAabb(origin, direction, entry.minimum, entry.maximum, best.distance)) continue;
                LocalHit candidate{};
                if (!colliderRaycast(world, entry.entity, origin, direction, best.distance, &candidate)) continue;
                if (candidate.distance > best.distance) continue;
                found = true;
                best.entity = entry.entity;
                best.distance = candidate.distance;
                best.position = add(origin, multiply(direction, candidate.distance));
                best.normal = candidate.normal;
            }
        } else {
            stack.push_back(node.left);
            stack.push_back(node.right);
        }
    }

    if (found && hit) *hit = best;
    return found;
}

bool sphereCast(
    const Ecs::World& world,
    Vec3 origin,
    float radius,
    Vec3 direction,
    float maximum_distance,
    RaycastHit *hit)
{
    radius = std::max(radius, 0.0f);
    if (maximum_distance <= 0.0f || lengthSquared(direction) <= 1.0e-20f) return false;
    direction = normalize(direction);
    syncBroadphase(world);

    const Vec3 end = add(origin, multiply(direction, maximum_distance));
    const Vec3 radius_vector {radius, radius, radius};
    const Vec3 query_min = subtract(minVec(origin, end), radius_vector);
    const Vec3 query_max = add(maxVec(origin, end), radius_vector);
    std::vector<Ecs::Entity> candidates;
    queryAabbCandidates(query_min, query_max, candidates);

    bool found = false;
    RaycastHit best{};
    best.distance = maximum_distance;
    for (const Ecs::Entity entity : candidates) {
        LocalHit candidate{};
        if (!colliderSphereCast(world, entity, origin, radius, direction, best.distance, &candidate)) continue;
        if (candidate.distance > best.distance) continue;
        found = true;
        best.entity = entity;
        best.distance = candidate.distance;
        best.position = add(origin, multiply(direction, candidate.distance));
        best.normal = candidate.normal;
    }

    if (found && hit) *hit = best;
    return found;
}

void overlapSphere(
    const Ecs::World& world,
    Vec3 center,
    float radius,
    std::vector<Ecs::Entity>& out)
{
    radius = std::max(radius, 0.0f);
    syncBroadphase(world);
    const Vec3 extent {radius, radius, radius};
    std::vector<Ecs::Entity> candidates;
    queryAabbCandidates(subtract(center, extent), add(center, extent), candidates);

    out.clear();
    out.reserve(candidates.size());
    for (const Ecs::Entity entity : candidates) {
        if (sphereOverlapsEntity(world, entity, center, radius)) out.push_back(entity);
    }
}

void overlapAabb(
    const Ecs::World& world,
    Vec3 minimum,
    Vec3 maximum,
    std::vector<Ecs::Entity>& out)
{
    syncBroadphase(world);
    const Vec3 query_min {
        std::min(minimum.x, maximum.x),
        std::min(minimum.y, maximum.y),
        std::min(minimum.z, maximum.z),
    };
    const Vec3 query_max {
        std::max(minimum.x, maximum.x),
        std::max(minimum.y, maximum.y),
        std::max(minimum.z, maximum.z),
    };
    queryAabbCandidates(query_min, query_max, out);
}

SpatialStats spatialStats(const Ecs::World& world)
{
    syncBroadphase(world);
    const BroadphaseCache& cache = broadphase();
    return {cache.entries.size(), cache.nodes.size(), cache.rebuilds};
}

} // namespace Physics
