#ifndef __APPLE__

#include "Renderer/Trace/OpenGL/TraceScene.hpp"

#include "Models/Models.hpp"
#include "Renderer/Scenes/Scene.hpp"
#include "Renderer/Scenes/SceneCache.hpp"
#include "Renderer/Visibility/Visibility.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Renderer::Trace::OpenGL {
namespace {

const char *label(const char *owner)
{
    return owner && owner[0] != '\0' ? owner : "Trace";
}

} // namespace

struct TraceScene::Impl {
    Scenes::SceneCache scene;
    Scenes::OpenGL::SceneResources resources;
    std::vector<Scenes::Scene::RenderItem> render_items;
    std::vector<std::uint32_t> visibility_mask;

    std::uint64_t world_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t model_resource_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t visibility_world_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t visibility_signature = 0u;
    bool visibility_all = true;

    TraceScene::SyncResult syncScene(const Ecs::World& world, const char *owner)
    {
        const std::uint64_t revision = world.changeRevision();
        const std::uint64_t resources_revision = Models::resourceRevision();
        if (revision == world_revision && resources_revision == model_resource_revision && resources.ready())
            return {true, false};

        Scenes::Scene::collectRenderItems(world, render_items);
        const std::uint64_t previous_geometry_revision = scene.geometryRevision();
        const std::uint64_t previous_resource_revision = scene.resourceRevision();

        std::string error;
        if (!scene.sync(world, render_items, Scenes::OpenGL::SceneResources::MaximumTextureSlots, &error)) {
            std::fprintf(stderr, "[%s]: scene cache failed: %s\n", label(owner), error.c_str());
            return {};
        }
        if (!resources.sync(scene, &error)) {
            std::fprintf(stderr, "[%s]: OpenGL scene upload failed: %s\n", label(owner), error.c_str());
            return {};
        }

        const bool scene_changed =
            scene.geometryRevision() != previous_geometry_revision ||
            scene.resourceRevision() != previous_resource_revision;
        if (scene_changed) {
            visibility_world_revision = std::numeric_limits<std::uint64_t>::max();
            std::fprintf(
                stderr,
                "[%s]: world cache %zu triangles, %zu nodes, %zu materials\n",
                label(owner),
                scene.triangles().size(),
                scene.nodes().size(),
                scene.materials().size()
            );
        }

        world_revision = revision;
        model_resource_revision = resources_revision;
        return {true, scene_changed};
    }

    bool syncVisibility(const Ecs::World& world, int width, int height, const char *owner)
    {
        const std::uint64_t revision = world.changeRevision();
        const Scenes::CameraState camera = Scenes::cameraState(Scenes::Scene::cameraState(world));
        const std::uint64_t current_signature =
            Scenes::cameraSignature(camera) ^
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(width)) << 32u) ^
            static_cast<std::uint32_t>(height);
        if (revision == visibility_world_revision && current_signature == visibility_signature)
            return true;

        const Visibility::Result visibility = Visibility::system().buildEntityMask(
            world, width, height, visibility_mask);
        std::string error;
        if (!resources.syncVisibility(visibility_mask, &error)) {
            std::fprintf(
                stderr,
                "[%s]: OpenGL visibility upload failed: %s\n",
                label(owner),
                error.c_str()
            );
            return false;
        }
        visibility_all = visibility.culled.empty();
        visibility_world_revision = revision;
        visibility_signature = current_signature;
        return true;
    }

    void clear()
    {
        resources.clear();
        scene.clear();
        render_items.clear();
        visibility_mask.clear();
        world_revision = std::numeric_limits<std::uint64_t>::max();
        model_resource_revision = std::numeric_limits<std::uint64_t>::max();
        visibility_world_revision = std::numeric_limits<std::uint64_t>::max();
        visibility_signature = 0u;
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

void TraceScene::bind() const
{
    if (impl_) impl_->resources.bind();
}

void TraceScene::clear()
{
    if (impl_) impl_->clear();
}

std::size_t TraceScene::nodeCount() const
{
    return impl_ ? impl_->scene.nodes().size() : 0u;
}

std::size_t TraceScene::triangleCount() const
{
    return impl_ ? impl_->scene.triangles().size() : 0u;
}

std::size_t TraceScene::materialCount() const
{
    return impl_ ? impl_->scene.materials().size() : 0u;
}

bool TraceScene::visibilityAll() const
{
    return impl_ && impl_->visibility_all;
}

} // namespace Renderer::Trace::OpenGL

#endif
