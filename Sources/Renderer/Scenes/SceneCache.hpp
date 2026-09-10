#ifndef RW_ENGINE_RENDERER_SCENES_SCENE_CACHE_HPP
#define RW_ENGINE_RENDERER_SCENES_SCENE_CACHE_HPP

#include "Ecs/Ecs.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Renderer::Scenes {

inline constexpr std::uint32_t LeafBit = 0x80000000u;

struct alignas(16) GpuNode {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float min_z = 0.0f;
    std::uint32_t first = 0u;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float max_z = 0.0f;
    std::uint32_t meta = 0u;
    std::array<std::uint32_t, 4> extra{};
};

struct alignas(16) GpuTriangle {
    std::array<float, 4> p0{};
    std::array<float, 4> p1{};
    std::array<float, 4> p2{};
    std::array<float, 4> n0{};
    std::array<float, 4> n1{};
    std::array<float, 4> n2{};
    std::array<float, 4> uv01{};
    std::array<float, 4> uv2{};
};

struct alignas(16) GpuMaterial {
    std::array<float, 4> base_color {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<std::int32_t, 4> data {-1, 0, 0, 0};
};

struct CameraState {
    bool valid = false;
    Vec3 position{};
    Vec3 forward {0.0f, 0.0f, -1.0f};
    Vec3 right {1.0f, 0.0f, 0.0f};
    Vec3 up {0.0f, 1.0f, 0.0f};
    float fov_degrees = 0.0f;
};

struct LightState {
    bool valid = false;
    LightType type = LightType::Point;
    Vec3 position{};
    Vec3 direction {0.0f, -1.0f, 0.0f};
    Vec3 color {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;
};

class SceneCache {
public:
    static void setLeafSize(std::uint32_t value) { leaf_size_ = value; }
    static void setMaximumTriangles(std::size_t value) { maximum_triangles_ = value; }
    static std::uint32_t leafSize() { return leaf_size_; }
    static std::size_t maximumTriangles() { return maximum_triangles_; }

    bool sync(
        const Ecs::World& world,
        const std::vector<Scene::RenderItem>& items,
        std::size_t maximum_texture_slots,
        std::string *error = nullptr
    );

    bool sync(
        const Ecs::World& world,
        std::size_t maximum_texture_slots,
        std::string *error = nullptr
    );

    std::uint64_t signature(
        const Ecs::World& world,
        const std::vector<Scene::RenderItem>& items
    ) const;

    void clear();

    const std::vector<GpuNode>& nodes() const { return nodes_; }
    const std::vector<GpuTriangle>& triangles() const { return triangles_; }
    const std::vector<GpuMaterial>& materials() const { return materials_; }
    const std::vector<Models::TextureHandle>& textureHandles() const { return texture_handles_; }
    const std::vector<Scene::RenderItem>& renderItems() const { return render_items_; }

private:
    std::uint32_t buildNode(std::uint32_t start, std::uint32_t count);

    inline static std::uint32_t leaf_size_ = 0u;
    inline static std::size_t maximum_triangles_ = 0u;

    std::vector<GpuNode> nodes_;
    std::vector<GpuTriangle> triangles_;
    std::vector<GpuMaterial> materials_;
    std::vector<Models::TextureHandle> texture_handles_;
    std::vector<Scene::RenderItem> render_items_;
};

CameraState cameraState(const Scene::CameraState& source);
LightState lightState(const Scene::LightState& source);
std::uint64_t cameraSignature(const CameraState& camera);
std::uint64_t lightSignature(const LightState& light);

static_assert(sizeof(GpuNode) == 48u);
static_assert(sizeof(GpuTriangle) == 128u);
static_assert(sizeof(GpuMaterial) == 32u);

} // namespace Renderer::Scenes

#endif
