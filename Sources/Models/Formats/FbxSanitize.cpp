#include "Models/Formats/FbxSanitize.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Models::Fbx {
namespace {

constexpr float kEpsilon = 1.0e-12f;

bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float lengthSquared(Vec3 value)
{
    return dot(value, value);
}

Vec3 normalized(Vec3 value, Vec3 fallback)
{
    if (!finite(value)) return fallback;
    const float squared = lengthSquared(value);
    if (squared <= kEpsilon) return fallback;
    const float inverse = 1.0f / std::sqrt(squared);
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

bool matrixNear(const Animation::Mat4& a, const Animation::Mat4& b)
{
    for (std::size_t index = 0u; index < a.value.size(); ++index) {
        if (std::abs(a.value[index] - b.value[index]) > 1.0e-4f) return false;
    }
    return true;
}

void synchronizeSkeletonFallbackPalette(Document *document)
{
    if (!document || !document->has_skeleton || document->skeleton.bones.empty()) return;

    const std::vector<Animation::Mat4> *common = nullptr;
    for (const Part& part : document->parts) {
        const auto& palette = part.mesh.skin_inverse_bind;
        if (palette.empty()) continue;
        if (palette.size() != document->skeleton.bones.size()) return;
        if (!common) {
            common = &palette;
            continue;
        }
        for (std::size_t bone = 0u; bone < palette.size(); ++bone) {
            if (!matrixNear((*common)[bone], palette[bone])) return;
        }
    }
    if (!common) return;

    // Animation::Pose keeps a skeleton-level fallback skin palette. When all
    // mesh cluster palettes agree (the normal case for one FBX skin split into
    // material parts), use the actual FBX cluster bind matrices rather than an
    // inverse-model approximation. Per-mesh palettes remain stored on MeshData.
    for (std::size_t bone = 0u; bone < common->size(); ++bone) {
        document->skeleton.bones[bone].inverse_bind = (*common)[bone];
    }
}

void recomputeBounds(MeshData *mesh)
{
    if (!mesh || mesh->vertices.empty()) {
        if (mesh) mesh->bounds = {};
        return;
    }

    const float infinity = std::numeric_limits<float>::infinity();
    Bounds bounds{{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
    for (const Vertex& vertex : mesh->vertices) {
        if (!finite(vertex.position)) continue;
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }

    if (!finite(bounds.minimum) || !finite(bounds.maximum)) bounds = {};
    mesh->bounds = bounds;
}

void sanitizeMesh(MeshData *mesh)
{
    if (!mesh) return;

    std::vector<std::uint32_t> valid_indices;
    valid_indices.reserve(mesh->indices.size());

    for (std::size_t offset = 0u; offset + 2u < mesh->indices.size(); offset += 3u) {
        const std::uint32_t i0 = mesh->indices[offset + 0u];
        const std::uint32_t i1 = mesh->indices[offset + 1u];
        const std::uint32_t i2 = mesh->indices[offset + 2u];
        if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;

        Vertex& v0 = mesh->vertices[i0];
        Vertex& v1 = mesh->vertices[i1];
        Vertex& v2 = mesh->vertices[i2];
        if (!finite(v0.position) || !finite(v1.position) || !finite(v2.position)) continue;

        const Vec3 geometric = cross(
            subtract(v1.position, v0.position),
            subtract(v2.position, v0.position)
        );
        if (lengthSquared(geometric) <= kEpsilon) continue;
        const Vec3 face = normalized(geometric, {0.0f, 0.0f, 1.0f});

        Vertex *corners[3] = {&v0, &v1, &v2};
        for (Vertex *vertex : corners) {
            Vec3 normal = normalized(vertex->normal, face);
            // FBX exporters occasionally leave normal data in the opposite
            // transform parity after a mirrored/geometric transform. Keep the
            // authored smooth direction, but force it into the triangle's
            // geometric hemisphere instead of letting adjacent faces go black.
            if (dot(normal, face) < 0.0f) {
                normal = {-normal.x, -normal.y, -normal.z};
            }
            vertex->normal = normal;
            if (!std::isfinite(vertex->uv.x) || !std::isfinite(vertex->uv.y)) vertex->uv = {};
        }

        valid_indices.push_back(i0);
        valid_indices.push_back(i1);
        valid_indices.push_back(i2);
    }

    mesh->indices = std::move(valid_indices);
    recomputeBounds(mesh);
}

} // namespace

void sanitize(Document *document)
{
    if (!document) return;
    for (Part& part : document->parts) sanitizeMesh(&part.mesh);
    document->parts.erase(
        std::remove_if(
            document->parts.begin(),
            document->parts.end(),
            [](const Part& part) { return part.mesh.indices.empty(); }
        ),
        document->parts.end()
    );
    synchronizeSkeletonFallbackPalette(document);
}

} // namespace Models::Fbx
