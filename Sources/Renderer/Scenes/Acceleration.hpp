#ifndef HORSE_RENDERER_SCENES_ACCELERATION_HPP
#define HORSE_RENDERER_SCENES_ACCELERATION_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Renderer::Scenes {

struct alignas(16) AccelerationTriangle {
    std::array<float, 4> p0{};
    std::array<float, 4> p1{};
    std::array<float, 4> p2{};
    std::array<float, 4> n0{};
    std::array<float, 4> n1{};
    std::array<float, 4> n2{};
    std::array<float, 4> uv01{};
    std::array<float, 4> uv2{};
};

struct AccelerationBlas {
    Models::MeshHandle mesh = Models::INVALID_MESH;
    std::uint32_t node_offset = 0u;
    std::uint32_t node_count = 0u;
    std::uint32_t triangle_offset = 0u;
    std::uint32_t triangle_count = 0u;
    std::uint64_t mesh_revision = 0u;
};

struct alignas(16) AccelerationInstance {
    Math::Mat4 object_to_world = Math::identityMatrix();
    Math::Mat4 world_to_object = Math::identityMatrix();
    std::array<float, 4> bounds_min{};
    std::array<float, 4> bounds_max{};
    std::array<std::uint32_t, 4> data{
        Ecs::INVALID_ENTITY,
        UINT32_MAX,
        UINT32_MAX,
        Models::INVALID_MATERIAL,
    };

    Ecs::Entity entity() const { return data[0]; }
    std::uint32_t instanceIndex() const { return data[1]; }
    std::uint32_t blasIndex() const { return data[2]; }
    Models::MaterialHandle material() const { return data[3]; }
};

class AccelerationScene {
public:
    bool sync(
        const Ecs::World& world,
        const std::vector<Scene::RenderItem>& items,
        std::string *error = nullptr
    );
    void clear();

    static bool eligible(const Ecs::World& world, const Scene::RenderItem& item);

    const std::vector<GpuNode>& tlasNodes() const { return tlas_nodes_; }
    const std::vector<GpuNode>& blasNodes() const { return blas_nodes_; }
    const std::vector<AccelerationTriangle>& localTriangles() const { return local_triangles_; }
    const std::vector<AccelerationBlas>& blases() const { return blases_; }
    const std::vector<AccelerationInstance>& instances() const { return instances_; }

    std::uint64_t blasRevision() const { return blas_revision_; }
    std::uint64_t tlasRevision() const { return tlas_revision_; }

private:
    struct CachedBlas {
        Models::MeshHandle mesh = Models::INVALID_MESH;
        std::uint64_t mesh_revision = 0u;
        std::vector<GpuNode> nodes;
        std::vector<AccelerationTriangle> triangles;
    };

    bool buildBlas(Models::MeshHandle mesh, CachedBlas *out, std::string *error);
    void flattenBlases(const std::vector<Models::MeshHandle>& meshes);
    void rebuildTlas(std::vector<AccelerationInstance> instances);

    std::unordered_map<Models::MeshHandle, CachedBlas> blas_cache_;
    std::vector<GpuNode> tlas_nodes_;
    std::vector<GpuNode> blas_nodes_;
    std::vector<AccelerationTriangle> local_triangles_;
    std::vector<AccelerationBlas> blases_;
    std::vector<AccelerationInstance> instances_;
    std::uint64_t blas_revision_ = 0u;
    std::uint64_t tlas_revision_ = 0u;
    std::uint64_t tlas_signature_ = 0u;
};

static_assert(sizeof(AccelerationTriangle) == 128u);
static_assert(sizeof(AccelerationInstance) == 176u);

} // namespace Renderer::Scenes

#endif
