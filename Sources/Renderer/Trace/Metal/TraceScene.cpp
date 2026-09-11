#ifdef __APPLE__

#include "Renderer/Trace/Metal/TraceScene.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Scenes/Metal/SceneResources.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <lwmgl/lwmgl.h>

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Renderer::Trace::Metal {

namespace {

struct PackedPosition {
    float x;
    float y;
    float z;
};

static_assert(sizeof(PackedPosition) == 12u);

const char *label(const char *owner)
{
    return owner && owner[0] != '\0' ? owner : "Trace";
}

} // namespace

struct TraceScene::Impl {
    Scenes::SceneCache scene;
    Scenes::Metal::SceneResources resources;
    std::vector<Scenes::Scene::RenderItem> render_items;
    std::vector<std::uint32_t> visibility_mask;

    Scenes::Scene::RenderRevision render_revision{};
    Scenes::Scene::RenderRevision visibility_render_revision{};
    std::uint64_t visibility_signature = 0u;
    bool has_alpha_cutouts = true;
    bool visibility_all = true;

    LWMGLBuffer acceleration_vertex_buffer = nullptr;
    LWMGLAccelerationStructure acceleration_structure = nullptr;

    void updateAlphaCutoutState()
    {
        has_alpha_cutouts = false;
        for (const Scenes::Scene::RenderItem& item : render_items) {
            const Models::MaterialData *material = item.material;
            if (!material) continue;
            if (material->opacity < 1.0f) {
                has_alpha_cutouts = true;
                return;
            }
            if (material->diffuse_texture == Models::INVALID_TEXTURE) continue;
            const Models::TextureAsset *texture = Models::texture(material->diffuse_texture);
            if (texture && texture->image.meaningful_alpha) {
                has_alpha_cutouts = true;
                return;
            }
        }
    }

    void destroyAccelerationStructure()
    {
        if (acceleration_structure) ::Metal.destroyAccelerationStructure(acceleration_structure);
        if (acceleration_vertex_buffer) ::Metal.destroyBuffer(acceleration_vertex_buffer);
        acceleration_structure = nullptr;
        acceleration_vertex_buffer = nullptr;
    }

    bool createAccelerationStructure()
    {
        destroyAccelerationStructure();
        if (scene.triangles().empty()) return true;

        std::vector<PackedPosition> positions;
        positions.reserve(scene.triangles().size() * 3u);
        for (const Scenes::GpuTriangle& triangle : scene.triangles()) {
            positions.push_back({triangle.p0[0], triangle.p0[1], triangle.p0[2]});
            positions.push_back({triangle.p1[0], triangle.p1[1], triangle.p1[2]});
            positions.push_back({triangle.p2[0], triangle.p2[1], triangle.p2[2]});
        }

        if (positions.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
            return false;

        const LWMGLBufferDesc vertex_desc = {
            positions.size() * sizeof(PackedPosition),
            LWMGL_STORAGE_SHARED
        };
        acceleration_vertex_buffer = ::Metal.createBuffer(&vertex_desc, positions.data());
        if (!acceleration_vertex_buffer) return false;

        const LWMGLTriangleGeometryDesc geometry = {
            acceleration_vertex_buffer,
            0u,
            static_cast<std::uint32_t>(sizeof(PackedPosition)),
            static_cast<std::uint32_t>(positions.size()),
            nullptr,
            0u,
            0u,
            1u
        };
        acceleration_structure = ::Metal.createTriangleAccelerationStructure(&geometry, 1u);
        if (!acceleration_structure) {
            ::Metal.destroyBuffer(acceleration_vertex_buffer);
            acceleration_vertex_buffer = nullptr;
            return false;
        }
        return true;
    }

    TraceScene::SyncResult syncScene(const Ecs::World& world, const char *owner)
    {
        const Scenes::Scene::RenderRevision revision = Scenes::Scene::renderRevision(world);
        if (revision == render_revision &&
            resources.ready() && (scene.triangles().empty() || acceleration_structure))
        {
            return {true, false};
        }

        Scenes::Scene::collectRenderItems(world, render_items);
        const std::uint64_t previous_geometry_revision = scene.geometryRevision();
        const std::uint64_t previous_resource_revision = scene.resourceRevision();

        std::string error;
        if (!scene.sync(world, render_items, Scenes::Metal::SceneResources::MaximumTextureSlots, &error)) {
            std::fprintf(stderr, "[%s]: scene cache failed: %s\n", label(owner), error.c_str());
            return {};
        }
        updateAlphaCutoutState();

        if (!resources.sync(scene, &error)) {
            std::fprintf(stderr, "[%s]: Metal scene upload failed: %s\n", label(owner), error.c_str());
            return {};
        }

        const bool geometry_changed = scene.geometryRevision() != previous_geometry_revision;
        const bool resources_changed = scene.resourceRevision() != previous_resource_revision;
        if (geometry_changed || (!scene.triangles().empty() && !acceleration_structure)) {
            if (!createAccelerationStructure()) {
                std::fprintf(
                    stderr,
                    "[%s]: Metal acceleration structure failed: %s\n",
                    label(owner),
                    lwmglGetLastError()
                );
                return {};
            }
        }

        const bool scene_changed = geometry_changed || resources_changed;
        if (scene_changed) {
            visibility_render_revision = {};
            std::fprintf(
                stderr,
                "[%s]: Metal native AS %zu triangles, %zu materials, alpha=%s\n",
                label(owner),
                scene.triangles().size(),
                scene.materials().size(),
                has_alpha_cutouts ? "cutout" : "opaque"
            );
        }

        render_revision = revision;
        return {true, scene_changed};
    }

    bool syncVisibility(const Ecs::World& world, int width, int height, const char *owner)
    {
        const Scenes::Scene::RenderRevision revision = Scenes::Scene::renderRevision(world);
        const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
        const std::uint64_t current_signature =
            Scenes::cameraSignature(camera) ^
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width)) << 32u) ^
            static_cast<std::uint32_t>(height);
        if (revision == visibility_render_revision && current_signature == visibility_signature)
            return true;

        const Visibility::Result visibility = Visibility::system().buildEntityMask(
            world, width, height, visibility_mask);
        std::string error;
        if (!resources.syncVisibility(visibility_mask, &error)) {
            std::fprintf(
                stderr,
                "[%s]: Metal visibility upload failed: %s\n",
                label(owner),
                error.c_str()
            );
            return false;
        }
        visibility_all = visibility.culled.empty();
        visibility_render_revision = revision;
        visibility_signature = current_signature;
        return true;
    }

    void clear()
    {
        destroyAccelerationStructure();
        resources.clear();
        scene.clear();
        render_items.clear();
        visibility_mask.clear();
        render_revision = {};
        visibility_render_revision = {};
        visibility_signature = 0u;
        has_alpha_cutouts = true;
        visibility_all = true;
    }
};

TraceScene::TraceScene() : impl_(new Impl) {}

TraceScene::~TraceScene()
{
    clear();
    delete impl_;
    impl_ = nullptr;
}

bool TraceScene::init(std::string *error)
{
    return impl_ && impl_->resources.init(error);
}

TraceScene::SyncResult TraceScene::sync(
    const Ecs::World& world,
    int width,
    int height,
    const char *owner)
{
    if (!impl_) return {};
    SyncResult result = impl_->syncScene(world, owner);
    if (!result.ok || !impl_->syncVisibility(world, width, height, owner)) return {};
    return result;
}

bool TraceScene::bind(LWMGLCommand command, std::uint32_t first_texture_binding) const
{
    return impl_ && impl_->resources.bind(command, first_texture_binding);
}

void TraceScene::clear()
{
    if (impl_) impl_->clear();
}

std::size_t TraceScene::triangleCount() const
{
    return impl_ ? impl_->scene.triangles().size() : 0u;
}

std::size_t TraceScene::materialCount() const
{
    return impl_ ? impl_->scene.materials().size() : 0u;
}

bool TraceScene::hasAlphaCutouts() const
{
    return impl_ && impl_->has_alpha_cutouts;
}

bool TraceScene::visibilityAll() const
{
    return impl_ && impl_->visibility_all;
}

LWMGLAccelerationStructure TraceScene::accelerationStructure() const
{
    return impl_ ? impl_->acceleration_structure : nullptr;
}

} // namespace Renderer::Trace::Metal

#endif
