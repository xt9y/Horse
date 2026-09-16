#include "Renderer/Rasterizer/RasterGeometrySDLGPU.hpp"

#include "Animation/Animation.hpp"
#include "Camera/Camera.hpp"
#include "Models/Models.hpp"
#include "Models/Core/MeshRevision.hpp"
#include "Renderer/DynamicGeometry.hpp"
#include "Renderer/Math.hpp"
#include "Renderer/SDLGPU/Context.hpp"
#include "Renderer/Scenes/Scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Renderer::RasterizerSDLGPU {
namespace {

using Float4 = std::array<float, 4>;
using Uint4 = std::array<std::uint32_t, 4>;

struct alignas(16) RasterVertex {
    Float4 position{};
    Float4 normal{};
    Float4 uv{};
    Uint4 meta{}; // item, influence offset, influence count, reserved
};

struct alignas(16) RasterInfluence {
    std::uint32_t joint = 0u;
    float weight = 0.0f;
    std::uint32_t reserved0 = 0u;
    std::uint32_t reserved1 = 0u;
};

struct alignas(16) RasterItem {
    Float4 model0{};
    Float4 model1{};
    Float4 model2{};
    Float4 model3{};
    Float4 inverse0{};
    Float4 inverse1{};
    Float4 inverse2{};
    Float4 inverse3{};
    Uint4 meta{}; // material, entity, skin matrix offset, skin matrix count
    Uint4 flags{}; // shadow caster, reserved
};

struct ItemBinding {
    std::size_t first_vertex = 0u;
    std::size_t vertex_count = 0u;
    bool morph = false;
};

static_assert(sizeof(RasterVertex) == 64u);
static_assert(sizeof(RasterInfluence) == 16u);
static_assert(sizeof(RasterItem) == 160u);

bool fail(std::string *error, const char *message)
{
    if (error) *error = message;
    return false;
}

Float4 column(const Math::Mat4& matrix, std::size_t index)
{
    const std::size_t offset = index * 4u;
    return {
        matrix[offset + 0u],
        matrix[offset + 1u],
        matrix[offset + 2u],
        matrix[offset + 3u],
    };
}

void appendMatrix(const Math::Mat4& matrix, std::vector<Float4>& output)
{
    output.push_back(column(matrix, 0u));
    output.push_back(column(matrix, 1u));
    output.push_back(column(matrix, 2u));
    output.push_back(column(matrix, 3u));
}

void hashValue(std::uint64_t& hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
}

void hashFloat(std::uint64_t& hash, float value)
{
    hashValue(hash, std::bit_cast<std::uint32_t>(value));
}

bool cameraAttached(const Ecs::World& world, Ecs::Entity entity, Ecs::Entity camera)
{
    if (camera == Ecs::INVALID_ENTITY || entity == Ecs::INVALID_ENTITY) return false;
    Ecs::Entity current = entity;
    const std::size_t maximum = world.size() + 1u;
    for (std::size_t depth = 0u; depth < maximum; ++depth) {
        if (current == camera) return true;
        const Parent *parent = world.get<Parent>(current);
        if (!parent || parent->entity == Ecs::INVALID_ENTITY || !world.alive(parent->entity))
            return false;
        current = parent->entity;
    }
    return false;
}

bool cameraLayer(
    const Ecs::World& world,
    const Scenes::Scene::RenderItem& item,
    Ecs::Entity camera)
{
    return item.layer == RenderLayer::Overlay || cameraAttached(world, item.entity, camera);
}

std::uint64_t shadowSignature(
    const Ecs::World& world,
    const std::vector<Scenes::Scene::RenderItem>& items)
{
    std::uint64_t hash = 1469598103934665603ull;
    const Ecs::Entity camera = Camera::activeCamera(world);
    for (const Scenes::Scene::RenderItem& item : items) {
        if (!item.mesh_component || !item.transform || !item.mesh ||
            cameraLayer(world, item, camera))
            continue;

        hashValue(hash, item.entity);
        hashValue(hash, item.instance_index);
        hashValue(hash, item.mesh_component->mesh);
        hashValue(hash, Models::Internal::meshRevision(item.mesh_component->mesh));
        hashValue(hash, item.mesh_component->material);
        if (item.material) hashFloat(hash, item.material->opacity);

        const Math::Mat4 model = Math::modelMatrix(*item.transform);
        for (const float value : model) hashFloat(hash, value);

        if (const ModelDeformComponent *deform = world.get<ModelDeformComponent>(item.entity)) {
            hashValue(hash, deform->model);
            hashValue(hash, deform->part);
            hashValue(hash, deform->pose_entity);
            if (const ModelPoseComponent *pose = world.get<ModelPoseComponent>(deform->pose_entity))
                hashValue(hash, pose->revision);
        }

        if (const Animation::SkinBindingComponent *binding =
                world.get<Animation::SkinBindingComponent>(item.entity)) {
            hashValue(hash, binding->animator);
            if (const Animation::AnimatorComponent *animator =
                    world.get<Animation::AnimatorComponent>(binding->animator))
                hashValue(hash, animator->pose.revision);
        }
    }
    return hash;
}

std::uint64_t topologySignature(const std::vector<Scenes::Scene::RenderItem>& items)
{
    std::uint64_t hash = 1469598103934665603ull;
    hashValue(hash, Models::resourceRevision());
    hashValue(hash, items.size());
    for (const Scenes::Scene::RenderItem& item : items) {
        hashValue(hash, item.entity);
        hashValue(hash, item.instance_index);
        hashValue(hash, item.mesh_component ? item.mesh_component->mesh : Models::INVALID_MESH);
        hashValue(hash, item.mesh_component ? item.mesh_component->material : Models::INVALID_MATERIAL);
        hashValue(hash, item.mesh ? item.mesh->vertices.size() : 0u);
        hashValue(hash, item.mesh ? item.mesh->indices.size() : 0u);
    }
    return hash;
}

std::uint64_t cameraLayerSignature(
    const Ecs::World& world,
    const std::vector<Scenes::Scene::RenderItem>& items)
{
    std::uint64_t hash = 1469598103934665603ull;
    const Ecs::Entity camera = Camera::activeCamera(world);
    hashValue(hash, camera);
    for (const Scenes::Scene::RenderItem& item : items) {
        hashValue(hash, item.entity);
        hashValue(hash, cameraLayer(world, item, camera) ? 1u : 0u);
    }
    return hash;
}

bool appendInfluences(
    const Models::MeshData& mesh,
    std::size_t vertex_index,
    std::vector<RasterInfluence>& influences,
    std::uint32_t *offset,
    std::uint32_t *count,
    std::string *error)
{
    if (!offset || !count || vertex_index >= mesh.vertices.size())
        return fail(error, "raster geometry references invalid source vertex");
    if (influences.size() > UINT32_MAX)
        return fail(error, "raster influence buffer exceeds backend index range");

    *offset = static_cast<std::uint32_t>(influences.size());
    const std::size_t set_count = std::min(mesh.joint_sets.size(), mesh.weight_sets.size());
    if (set_count != 0u) {
        for (std::size_t set = 0u; set < set_count; ++set) {
            if (vertex_index >= mesh.joint_sets[set].size() || vertex_index >= mesh.weight_sets[set].size())
                return fail(error, "raster skin attribute set count mismatch");
            const Models::Joint4 joints = mesh.joint_sets[set][vertex_index];
            const Models::Vec4 weights = mesh.weight_sets[set][vertex_index];
            const std::array<std::uint16_t, 4> joint_values{{joints.x, joints.y, joints.z, joints.w}};
            const std::array<float, 4> weight_values{{weights.x, weights.y, weights.z, weights.w}};
            for (std::size_t component = 0u; component < 4u; ++component) {
                if (weight_values[component] == 0.0f) continue;
                influences.push_back(RasterInfluence{
                    joint_values[component],
                    weight_values[component],
                    0u,
                    0u,
                });
            }
        }
    } else {
        const Animation::SkinWeights& skin = mesh.vertices[vertex_index].skin;
        for (std::size_t component = 0u; component < skin.joints.size(); ++component) {
            if (skin.weights[component] == 0.0f) continue;
            influences.push_back(RasterInfluence{
                skin.joints[component],
                skin.weights[component],
                0u,
                0u,
            });
        }
    }

    const std::size_t influence_count = influences.size() - *offset;
    if (influence_count > UINT32_MAX)
        return fail(error, "raster vertex influence count exceeds backend index range");
    *count = static_cast<std::uint32_t>(influence_count);
    return true;
}

bool sourceVertex(
    const Ecs::World& world,
    const Scenes::Scene::RenderItem& item,
    std::size_t source_index,
    Models::Vertex *output,
    std::string *error)
{
    if (!output || !item.mesh || source_index >= item.mesh->vertices.size())
        return fail(error, "raster geometry references invalid model vertex");
    *output = item.mesh->vertices[source_index];

    const ModelDeformComponent *deform = world.get<ModelDeformComponent>(item.entity);
    if (!deform || item.mesh->morph_targets.empty()) return true;
    const ModelPoseComponent *pose = world.get<ModelPoseComponent>(deform->pose_entity);
    const Models::ModelPart *part = Models::part(deform->model, deform->part);
    if (!pose || !part)
        return fail(error, "raster dynamic model geometry has no pose state");

    std::vector<float> weights = item.mesh->morph_weights;
    if (part->node != Models::INVALID_INDEX && part->node < pose->pose.nodes.size() &&
        !pose->pose.nodes[part->node].weights.empty())
        weights = pose->pose.nodes[part->node].weights;
    if (weights.size() < item.mesh->morph_targets.size())
        weights.resize(item.mesh->morph_targets.size(), 0.0f);

    for (std::size_t target_index = 0u; target_index < item.mesh->morph_targets.size(); ++target_index) {
        const float weight = weights[target_index];
        if (weight == 0.0f) continue;
        const Models::MorphTargetData& target = item.mesh->morph_targets[target_index];
        if (!target.positions.empty()) {
            if (target.positions.size() != item.mesh->vertices.size())
                return fail(error, "raster morph POSITION count mismatch");
            output->position.x += target.positions[source_index].x * weight;
            output->position.y += target.positions[source_index].y * weight;
            output->position.z += target.positions[source_index].z * weight;
        }
        if (!target.normals.empty()) {
            if (target.normals.size() != item.mesh->vertices.size())
                return fail(error, "raster morph NORMAL count mismatch");
            output->normal.x += target.normals[source_index].x * weight;
            output->normal.y += target.normals[source_index].y * weight;
            output->normal.z += target.normals[source_index].z * weight;
        }
    }
    return true;
}

bool appendModelSkin(
    const Ecs::World& world,
    const Scenes::Scene::RenderItem& item,
    std::vector<Float4>& matrices,
    std::uint32_t *offset,
    std::uint32_t *count,
    std::string *error)
{
    if (!offset || !count) return false;
    *offset = 0u;
    *count = 0u;

    const ModelDeformComponent *deform = world.get<ModelDeformComponent>(item.entity);
    if (deform) {
        const ModelPoseComponent *pose = world.get<ModelPoseComponent>(deform->pose_entity);
        const Models::ModelPart *part = Models::part(deform->model, deform->part);
        if (!pose || !part)
            return fail(error, "raster dynamic model geometry has no pose state");
        const Models::NodeData *node = part->node != Models::INVALID_INDEX
            ? Models::node(deform->model, part->node)
            : nullptr;
        if (!node || node->skin == Models::INVALID_INDEX) return true;
        const Models::SkinData *skin = Models::skin(deform->model, node->skin);
        if (!skin || part->node >= pose->pose.nodes.size())
            return fail(error, "raster model skin state is invalid");

        Math::Mat4 inverse_mesh{};
        if (!Math::inverseMatrix(pose->pose.nodes[part->node].world, &inverse_mesh))
            return fail(error, "raster model skin mesh transform is singular");
        if (matrices.size() / 4u > UINT32_MAX)
            return fail(error, "raster skin matrix buffer exceeds backend index range");
        *offset = static_cast<std::uint32_t>(matrices.size() / 4u);
        for (std::size_t joint = 0u; joint < skin->joints.size(); ++joint) {
            const std::uint32_t node_index = skin->joints[joint];
            if (node_index >= pose->pose.nodes.size())
                return fail(error, "raster model skin joint node is invalid");
            const Math::Mat4 inverse_bind = joint < skin->inverse_bind_matrices.size()
                ? skin->inverse_bind_matrices[joint]
                : Math::identityMatrix();
            appendMatrix(
                Math::multiply(
                    inverse_mesh,
                    Math::multiply(pose->pose.nodes[node_index].world, inverse_bind)),
                matrices);
        }
        if (skin->joints.size() > UINT32_MAX)
            return fail(error, "raster skin joint count exceeds backend index range");
        *count = static_cast<std::uint32_t>(skin->joints.size());
        return true;
    }

    const Animation::SkinBindingComponent *binding =
        world.get<Animation::SkinBindingComponent>(item.entity);
    if (!binding || binding->animator == Ecs::INVALID_ENTITY) return true;
    const Animation::AnimatorComponent *animator =
        world.get<Animation::AnimatorComponent>(binding->animator);
    if (!animator || animator->pose.skin.empty()) return true;
    if (matrices.size() / 4u > UINT32_MAX || animator->pose.skin.size() > UINT32_MAX)
        return fail(error, "raster animation skin matrix buffer exceeds backend index range");
    *offset = static_cast<std::uint32_t>(matrices.size() / 4u);
    *count = static_cast<std::uint32_t>(animator->pose.skin.size());
    for (const Animation::Mat4& matrix : animator->pose.skin) {
        Math::Mat4 value = matrix.value;
        appendMatrix(value, matrices);
    }
    return true;
}

bool updateVertexRange(
    const Ecs::World& world,
    const Scenes::Scene::RenderItem& item,
    const ItemBinding& binding,
    std::vector<RasterVertex>& vertices,
    std::string *error)
{
    if (!item.mesh || binding.first_vertex + binding.vertex_count > vertices.size())
        return fail(error, "raster geometry item binding is invalid");
    if (binding.vertex_count % 3u != 0u)
        return fail(error, "raster geometry topology changed during animation update");
    const std::size_t triangle_count = binding.vertex_count / 3u;
    if (triangle_count > item.mesh->indices.size() / 3u)
        return fail(error, "raster geometry topology changed during animation update");

    std::size_t output_index = binding.first_vertex;
    for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
        for (std::size_t corner = 0u; corner < 3u; ++corner) {
            const std::uint32_t source_index = item.mesh->indices[triangle * 3u + corner];
            Models::Vertex source{};
            if (!sourceVertex(world, item, source_index, &source, error)) return false;
            RasterVertex& target = vertices[output_index++];
            target.position = {source.position.x, source.position.y, source.position.z, 1.0f};
            target.normal = {source.normal.x, source.normal.y, source.normal.z, 0.0f};
            target.uv = {source.uv.x, source.uv.y, 0.0f, 0.0f};
        }
    }
    return true;
}

} // namespace

struct RasterGeometry::Impl {
    SDL_GPUBuffer *vertices_buffer = nullptr;
    SDL_GPUBuffer *items_buffer = nullptr;
    SDL_GPUBuffer *influences_buffer = nullptr;
    SDL_GPUBuffer *skin_buffer = nullptr;
    std::size_t vertices_capacity = 0u;
    std::size_t items_capacity = 0u;
    std::size_t influences_capacity = 0u;
    std::size_t skin_capacity = 0u;
    std::vector<RasterVertex> vertices;
    std::vector<RasterItem> items;
    std::vector<RasterInfluence> influences;
    std::vector<Float4> skin;
    std::vector<ItemBinding> bindings;
    std::uint64_t topology_signature = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t camera_layer_signature = std::numeric_limits<std::uint64_t>::max();
    Scenes::Scene::RenderRevision render_revision{};
    std::uint64_t camera_revision = 0u;
    bool revision_initialized = false;
    std::uint64_t geometry_revision = 0u;
    std::uint64_t shadow_signature = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t shadow_revision = 0u;
    std::size_t world_vertex_count = 0u;
    std::size_t camera_first_vertex = 0u;
    std::size_t camera_vertex_count = 0u;

    bool ensureBuffer(
        SDL_GPUBuffer *&buffer,
        std::size_t& capacity,
        std::size_t bytes,
        const char *label)
    {
        const std::size_t safe_bytes = std::max<std::size_t>(bytes, 16u);
        if (buffer && capacity >= safe_bytes) return true;
        SDL_GPUBuffer *replacement = SDLGPU::createBuffer(
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            safe_bytes,
            nullptr,
            label);
        if (!replacement) return false;
        if (buffer) SDL_ReleaseGPUBuffer(SDLGPU::device(), buffer);
        buffer = replacement;
        capacity = safe_bytes;
        return true;
    }

    bool rebuild(
        const Ecs::World& world,
        const Scenes::SceneCache& scene,
        std::string *error)
    {
        vertices.clear();
        influences.clear();
        bindings.clear();
        const auto& render_items = scene.renderItems();
        bindings.resize(render_items.size());

        const std::size_t maximum_triangles = Scenes::SceneCache::maximumTriangles();
        std::vector<std::size_t> triangle_counts(render_items.size(), 0u);
        std::size_t triangles_written = 0u;
        for (std::size_t item_index = 0u; item_index < render_items.size(); ++item_index) {
            const Scenes::Scene::RenderItem& item = render_items[item_index];
            if (!item.mesh_component || !item.mesh || !item.material ||
                item.material->opacity < Scenes::SceneCache::opacityCutoff())
                continue;
            const std::size_t available_triangles = item.mesh->indices.size() / 3u;
            const std::size_t triangle_count = maximum_triangles == 0u
                ? available_triangles
                : std::min(available_triangles, maximum_triangles - std::min(maximum_triangles, triangles_written));
            triangle_counts[item_index] = triangle_count;
            triangles_written += triangle_count;
            if (maximum_triangles != 0u && triangles_written >= maximum_triangles) break;
        }

        const Ecs::Entity camera = Camera::activeCamera(world);
        const auto append_layer = [&](bool camera_layer) -> bool {
            for (std::size_t item_index = 0u; item_index < render_items.size(); ++item_index) {
                const Scenes::Scene::RenderItem& item = render_items[item_index];
                if (cameraLayer(world, item, camera) != camera_layer) continue;

                ItemBinding& binding = bindings[item_index];
                binding.first_vertex = vertices.size();
                binding.morph = world.get<ModelDeformComponent>(item.entity) &&
                    item.mesh && !item.mesh->morph_targets.empty();
                const std::size_t triangle_count = triangle_counts[item_index];
                if (triangle_count == 0u || !item.mesh) continue;

                for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
                    for (std::size_t corner = 0u; corner < 3u; ++corner) {
                        const std::uint32_t source_index = item.mesh->indices[triangle * 3u + corner];
                        if (source_index >= item.mesh->vertices.size())
                            return fail(error, "raster mesh index references invalid vertex");
                        Models::Vertex source{};
                        if (!sourceVertex(world, item, source_index, &source, error)) return false;
                        std::uint32_t influence_offset = 0u;
                        std::uint32_t influence_count = 0u;
                        if (!appendInfluences(
                                *item.mesh,
                                source_index,
                                influences,
                                &influence_offset,
                                &influence_count,
                                error)) return false;
                        RasterVertex vertex;
                        vertex.position = {source.position.x, source.position.y, source.position.z, 1.0f};
                        vertex.normal = {source.normal.x, source.normal.y, source.normal.z, 0.0f};
                        vertex.uv = {source.uv.x, source.uv.y, 0.0f, 0.0f};
                        vertex.meta = {
                            static_cast<std::uint32_t>(item_index),
                            influence_offset,
                            influence_count,
                            0u,
                        };
                        vertices.push_back(vertex);
                    }
                }
                binding.vertex_count = vertices.size() - binding.first_vertex;
            }
            return true;
        };

        if (!append_layer(false)) return false;
        world_vertex_count = vertices.size();
        camera_first_vertex = world_vertex_count;
        if (!append_layer(true)) return false;
        camera_vertex_count = vertices.size() - camera_first_vertex;
        return true;
    }

    bool updateMorphs(
        const Ecs::World& world,
        const Scenes::SceneCache& scene,
        std::string *error)
    {
        const auto& render_items = scene.renderItems();
        if (render_items.size() != bindings.size())
            return fail(error, "raster geometry item count changed during animation update");
        for (std::size_t index = 0u; index < bindings.size(); ++index) {
            if (!bindings[index].morph || bindings[index].vertex_count == 0u) continue;
            if (!updateVertexRange(world, render_items[index], bindings[index], vertices, error))
                return false;
        }
        return true;
    }

    bool updateItems(
        const Ecs::World& world,
        const Scenes::SceneCache& scene,
        std::string *error)
    {
        const auto& render_items = scene.renderItems();
        const Ecs::Entity camera = Camera::activeCamera(world);
        items.assign(render_items.size(), RasterItem{});
        skin.clear();
        for (std::size_t index = 0u; index < render_items.size(); ++index) {
            const Scenes::Scene::RenderItem& source = render_items[index];
            RasterItem& target = items[index];
            const Math::Mat4 model = source.transform
                ? Math::modelMatrix(*source.transform)
                : Math::identityMatrix();
            const Math::Mat4 inverse = source.transform
                ? Math::inverseModelMatrix(*source.transform)
                : Math::identityMatrix();
            target.model0 = column(model, 0u);
            target.model1 = column(model, 1u);
            target.model2 = column(model, 2u);
            target.model3 = column(model, 3u);
            target.inverse0 = column(inverse, 0u);
            target.inverse1 = column(inverse, 1u);
            target.inverse2 = column(inverse, 2u);
            target.inverse3 = column(inverse, 3u);

            std::uint32_t skin_offset = 0u;
            std::uint32_t skin_count = 0u;
            if (!appendModelSkin(world, source, skin, &skin_offset, &skin_count, error))
                return false;
            target.meta = {
                source.mesh_component
                    ? scene.materialIndex(source.mesh_component->material)
                    : 0u,
                source.entity,
                skin_offset,
                skin_count,
            };
            const bool attached_to_camera = cameraLayer(world, source, camera);
            target.flags = {
                attached_to_camera ? 0u : 1u,
                0u,
                0u,
                0u,
            };
        }
        return true;
    }

    bool upload(
        bool upload_vertices,
        bool upload_items,
        bool upload_influences,
        bool upload_skin,
        std::string *error)
    {
        const std::size_t vertex_bytes = vertices.size() * sizeof(RasterVertex);
        const std::size_t item_bytes = items.size() * sizeof(RasterItem);
        const std::size_t influence_bytes = influences.size() * sizeof(RasterInfluence);
        const std::size_t skin_bytes = skin.size() * sizeof(Float4);
        if (!ensureBuffer(vertices_buffer, vertices_capacity, vertex_bytes, "Horse Raster Vertices") ||
            !ensureBuffer(items_buffer, items_capacity, item_bytes, "Horse Raster Items") ||
            !ensureBuffer(influences_buffer, influences_capacity, influence_bytes, "Horse Raster Influences") ||
            !ensureBuffer(skin_buffer, skin_capacity, skin_bytes, "Horse Raster Skin"))
            return fail(error, "failed to allocate raster geometry buffers");

        const bool have_upload =
            (upload_vertices && vertex_bytes != 0u) ||
            (upload_items && item_bytes != 0u) ||
            (upload_influences && influence_bytes != 0u) ||
            (upload_skin && skin_bytes != 0u);
        if (!have_upload) return true;

        SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(SDLGPU::device());
        if (!command) return fail(error, "failed to acquire raster geometry upload command buffer");
        const bool uploaded =
            (!upload_vertices || vertex_bytes == 0u ||
                SDLGPU::uploadBuffer(command, vertices_buffer, vertices.data(), vertex_bytes, true)) &&
            (!upload_items || item_bytes == 0u ||
                SDLGPU::uploadBuffer(command, items_buffer, items.data(), item_bytes, true)) &&
            (!upload_influences || influence_bytes == 0u ||
                SDLGPU::uploadBuffer(command, influences_buffer, influences.data(), influence_bytes, true)) &&
            (!upload_skin || skin_bytes == 0u ||
                SDLGPU::uploadBuffer(command, skin_buffer, skin.data(), skin_bytes, true));
        if (!uploaded || !SDL_SubmitGPUCommandBuffer(command)) {
            SDL_CancelGPUCommandBuffer(command);
            return fail(error, "failed to upload raster geometry buffers");
        }
        return true;
    }
};

RasterGeometry::~RasterGeometry()
{
    clear();
    delete impl_;
    impl_ = nullptr;
}

bool RasterGeometry::sync(
    const Ecs::World& world,
    const Scenes::SceneCache& scene,
    std::string *error)
{
    if (error) error->clear();
    if (!impl_) impl_ = new Impl;
    if (!SDLGPU::device()) return fail(error, "SDL_GPU device is not initialized");

    const Scenes::Scene::RenderRevision current_revision = Scenes::Scene::renderRevision(world);
    const std::uint64_t current_camera_revision =
        world.changeRevision(Ecs::ChangeKind::Camera);
    const std::uint64_t current_topology = topologySignature(scene.renderItems());
    const bool structure_changed = !impl_->revision_initialized ||
        impl_->render_revision.structure != current_revision.structure;
    const bool camera_changed = !impl_->revision_initialized ||
        impl_->camera_revision != current_camera_revision;
    const bool base_topology_changed = impl_->topology_signature != current_topology;
    std::uint64_t current_camera_layer = impl_->camera_layer_signature;
    if (base_topology_changed || structure_changed || camera_changed)
        current_camera_layer = cameraLayerSignature(world, scene.renderItems());
    const bool camera_layer_changed = impl_->camera_layer_signature != current_camera_layer;
    const bool topology_changed = base_topology_changed || camera_layer_changed;
    const bool animation_changed = !impl_->revision_initialized ||
        impl_->render_revision.animation != current_revision.animation;
    const bool transform_changed = !impl_->revision_initialized ||
        impl_->render_revision.transform != current_revision.transform;
    const bool resource_changed = !impl_->revision_initialized ||
        impl_->render_revision.resource != current_revision.resource;

    bool vertices_changed = false;
    bool influences_changed = false;
    if (topology_changed) {
        if (!impl_->rebuild(world, scene, error)) return false;
        vertices_changed = true;
        influences_changed = true;
    } else if (animation_changed) {
        if (!impl_->updateMorphs(world, scene, error)) return false;
        vertices_changed = std::any_of(
            impl_->bindings.begin(),
            impl_->bindings.end(),
            [](const ItemBinding& binding) { return binding.morph && binding.vertex_count != 0u; });
    }

    const bool items_changed =
        topology_changed || structure_changed || animation_changed || transform_changed ||
        resource_changed || camera_changed;
    if (items_changed && !impl_->updateItems(world, scene, error)) return false;
    if (!impl_->upload(
            vertices_changed,
            items_changed,
            influences_changed,
            items_changed,
            error)) return false;

    if (topology_changed || vertices_changed || items_changed || influences_changed)
        ++impl_->geometry_revision;
    const std::uint64_t current_shadow_signature = shadowSignature(world, scene.renderItems());
    if (impl_->shadow_signature != current_shadow_signature) {
        ++impl_->shadow_revision;
        impl_->shadow_signature = current_shadow_signature;
    }
    impl_->topology_signature = current_topology;
    impl_->camera_layer_signature = current_camera_layer;
    impl_->render_revision = current_revision;
    impl_->camera_revision = current_camera_revision;
    impl_->revision_initialized = true;
    return true;
}

void RasterGeometry::bind(SDL_GPURenderPass *pass, Uint32 first_slot) const
{
    if (!impl_ || !pass) return;
    SDL_GPUBuffer *buffers[] = {
        impl_->vertices_buffer,
        impl_->items_buffer,
        impl_->influences_buffer,
        impl_->skin_buffer,
    };
    SDL_BindGPUVertexStorageBuffers(pass, first_slot, buffers, 4u);
}

void RasterGeometry::clear()
{
    if (!impl_) return;
    SDL_GPUDevice *device = SDLGPU::device();
    if (device) {
        if (impl_->skin_buffer) SDL_ReleaseGPUBuffer(device, impl_->skin_buffer);
        if (impl_->influences_buffer) SDL_ReleaseGPUBuffer(device, impl_->influences_buffer);
        if (impl_->items_buffer) SDL_ReleaseGPUBuffer(device, impl_->items_buffer);
        if (impl_->vertices_buffer) SDL_ReleaseGPUBuffer(device, impl_->vertices_buffer);
    }
    impl_->skin_buffer = nullptr;
    impl_->influences_buffer = nullptr;
    impl_->items_buffer = nullptr;
    impl_->vertices_buffer = nullptr;
    impl_->skin_capacity = 0u;
    impl_->influences_capacity = 0u;
    impl_->items_capacity = 0u;
    impl_->vertices_capacity = 0u;
    impl_->skin.clear();
    impl_->influences.clear();
    impl_->items.clear();
    impl_->vertices.clear();
    impl_->bindings.clear();
    impl_->topology_signature = std::numeric_limits<std::uint64_t>::max();
    impl_->camera_layer_signature = std::numeric_limits<std::uint64_t>::max();
    impl_->camera_revision = 0u;
    impl_->revision_initialized = false;
    impl_->geometry_revision = 0u;
    impl_->shadow_signature = std::numeric_limits<std::uint64_t>::max();
    impl_->shadow_revision = 0u;
    impl_->world_vertex_count = 0u;
    impl_->camera_first_vertex = 0u;
    impl_->camera_vertex_count = 0u;
}

std::size_t RasterGeometry::vertexCount() const
{
    return impl_ ? impl_->vertices.size() : 0u;
}

std::size_t RasterGeometry::worldVertexCount() const
{
    return impl_ ? impl_->world_vertex_count : 0u;
}

std::size_t RasterGeometry::cameraFirstVertex() const
{
    return impl_ ? impl_->camera_first_vertex : 0u;
}

std::size_t RasterGeometry::cameraVertexCount() const
{
    return impl_ ? impl_->camera_vertex_count : 0u;
}

bool RasterGeometry::hasCameraGeometry() const
{
    return cameraVertexCount() != 0u;
}

std::uint64_t RasterGeometry::revision() const
{
    return impl_ ? impl_->geometry_revision : 0u;
}

std::uint64_t RasterGeometry::shadowRevision() const
{
    return impl_ ? impl_->shadow_revision : 0u;
}

} // namespace Renderer::RasterizerSDLGPU
