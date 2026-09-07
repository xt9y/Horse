#include "Models/Formats/Fbx.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/ThirdParty/Ufbx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Models::Fbx {
namespace {

constexpr float kTargetSampleRate = 30.0f;
constexpr std::size_t kMaximumClipSamples = 18000u;

std::string toString(ufbx_string value)
{
    return value.data && value.length > 0u
        ? std::string(value.data, value.length)
        : std::string{};
}

Vec3 toVec3(ufbx_vec3 value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
}

Vec2 toVec2(ufbx_vec2 value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
    };
}

Animation::Vec3 toAnimationVec3(ufbx_vec3 value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
}

Animation::Quat toAnimationQuat(ufbx_quat value)
{
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
        static_cast<float>(value.w),
    };
}

Animation::Transform toAnimationTransform(ufbx_transform value)
{
    return {
        toAnimationVec3(value.translation),
        toAnimationQuat(value.rotation),
        toAnimationVec3(value.scale),
    };
}

Animation::Mat4 toAnimationMatrix(ufbx_matrix value)
{
    Animation::Mat4 result;
    result.value = {
        static_cast<float>(value.m00),
        static_cast<float>(value.m10),
        static_cast<float>(value.m20),
        0.0f,

        static_cast<float>(value.m01),
        static_cast<float>(value.m11),
        static_cast<float>(value.m21),
        0.0f,

        static_cast<float>(value.m02),
        static_cast<float>(value.m12),
        static_cast<float>(value.m22),
        0.0f,

        static_cast<float>(value.m03),
        static_cast<float>(value.m13),
        static_cast<float>(value.m23),
        1.0f,
    };
    return result;
}

Vec3 normalize(Vec3 value)
{
    const float length_squared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (length_squared <= 1.0e-20f) return {0.0f, 0.0f, 1.0f};
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

Bounds calculateBounds(const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return {};

    const float maximum = std::numeric_limits<float>::max();
    Bounds bounds {{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
    for (const Vertex& vertex : vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    return bounds;
}

const ufbx_material *materialFor(
    const ufbx_node *node,
    const ufbx_mesh *mesh,
    std::uint32_t index)
{
    if (node && index < node->materials.count) return node->materials.data[index];
    if (index < mesh->materials.count) return mesh->materials.data[index];
    return nullptr;
}

const ufbx_texture *baseColorTexture(const ufbx_material *material)
{
    if (!material) return nullptr;
    if (material->pbr.base_color.texture) return material->pbr.base_color.texture;
    return material->fbx.diffuse_color.texture;
}

MaterialData convertMaterial(
    const ufbx_material *material,
    const std::filesystem::path& source_path)
{
    MaterialData result;
    if (!material) return result;

    result.name = toString(material->name);

    if (material->pbr.base_color.has_value) {
        const ufbx_vec4 value = material->pbr.base_color.value_vec4;
        result.color = {
            static_cast<float>(value.x),
            static_cast<float>(value.y),
            static_cast<float>(value.z),
        };
    } else if (material->fbx.diffuse_color.has_value) {
        result.color = toVec3(material->fbx.diffuse_color.value_vec3);
    }

    if (material->pbr.opacity.has_value) {
        result.opacity = std::clamp(
            static_cast<float>(material->pbr.opacity.value_vec3.x),
            0.0f,
            1.0f
        );
    } else if (material->fbx.transparency_factor.has_value) {
        result.opacity = 1.0f - std::clamp(
            static_cast<float>(material->fbx.transparency_factor.value_vec3.x),
            0.0f,
            1.0f
        );
    }

    const ufbx_texture *texture = baseColorTexture(material);
    if (!texture) return result;

    std::string filename = toString(texture->relative_filename);
    if (filename.empty()) filename = toString(texture->filename);

    if (texture->content.data && texture->content.size > 0u) {
        std::string texture_name = toString(texture->name);
        if (texture_name.empty()) texture_name = filename;
        if (texture_name.empty()) texture_name = result.name.empty() ? "embedded" : result.name;

        const std::string cache_key =
            source_path.lexically_normal().string() + "#embedded-texture:" + texture_name;
        std::string ignored_error;
        result.diffuse_texture = loadTextureMemory(
            cache_key,
            texture->content.data,
            texture->content.size,
            &ignored_error
        );
        if (result.diffuse_texture != INVALID_TEXTURE) {
            result.texture_path = cache_key;
            return result;
        }
    }

    if (!filename.empty()) {
        std::filesystem::path texture_path(filename);
        if (texture_path.is_relative()) texture_path = source_path.parent_path() / texture_path;
        texture_path = texture_path.lexically_normal();
        result.texture_path = texture_path.string();

        std::string ignored_error;
        result.diffuse_texture = loadTexture(result.texture_path, &ignored_error);
    }

    return result;
}

struct SkeletonBuild {
    std::vector<const ufbx_node *> nodes;
    std::unordered_map<const ufbx_node *, std::uint16_t> indices;
    bool overflow = false;

    void add(const ufbx_node *node)
    {
        if (!node || indices.find(node) != indices.end() || overflow) return;
        add(node->parent);
        if (overflow) return;

        if (nodes.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
            overflow = true;
            return;
        }

        const std::uint16_t index = static_cast<std::uint16_t>(nodes.size());
        nodes.push_back(node);
        indices.emplace(node, index);
    }
};

SkeletonBuild collectSkeleton(const ufbx_scene *scene)
{
    SkeletonBuild result;
    for (std::size_t skin_index = 0u; skin_index < scene->skin_deformers.count; ++skin_index) {
        const ufbx_skin_deformer *skin = scene->skin_deformers.data[skin_index];
        if (!skin) continue;

        for (std::size_t cluster_index = 0u; cluster_index < skin->clusters.count; ++cluster_index) {
            const ufbx_skin_cluster *cluster = skin->clusters.data[cluster_index];
            if (cluster && cluster->bone_node) result.add(cluster->bone_node);
        }
    }
    return result;
}

Animation::Skeleton makeSkeleton(
    const SkeletonBuild& build,
    const std::filesystem::path& source_path)
{
    Animation::Skeleton result;
    result.name = source_path.stem().string();
    result.bones.reserve(build.nodes.size());

    for (const ufbx_node *node : build.nodes) {
        Animation::Bone bone;
        bone.name = toString(node->name);
        bone.bind_local = toAnimationTransform(node->local_transform);

        if (node->parent) {
            const auto parent = build.indices.find(node->parent);
            if (parent != build.indices.end()) {
                bone.parent = static_cast<std::int32_t>(parent->second);
            }
        }

        const ufbx_matrix inverse_bind = ufbx_matrix_invert(&node->node_to_world);
        bone.inverse_bind = toAnimationMatrix(inverse_bind);
        result.bones.push_back(std::move(bone));
    }

    return result;
}

std::vector<Animation::AnimationClip> makeAnimations(
    const ufbx_scene *scene,
    const SkeletonBuild& skeleton)
{
    std::vector<Animation::AnimationClip> result;
    if (skeleton.nodes.empty()) return result;

    result.reserve(scene->anim_stacks.count);
    for (std::size_t animation_index = 0u; animation_index < scene->anim_stacks.count; ++animation_index) {
        const ufbx_anim_stack *stack = scene->anim_stacks.data[animation_index];
        if (!stack || !stack->anim) continue;

        const double duration_seconds = std::max(stack->time_end - stack->time_begin, 0.0);
        std::size_t sample_count = 1u;
        if (duration_seconds > 0.0) {
            sample_count = static_cast<std::size_t>(std::ceil(duration_seconds * kTargetSampleRate)) + 1u;
            sample_count = std::clamp<std::size_t>(sample_count, 2u, kMaximumClipSamples);
        }

        Animation::AnimationClip clip;
        clip.name = toString(stack->name);
        if (clip.name.empty()) clip.name = "Animation_" + std::to_string(animation_index);
        clip.duration = static_cast<float>(duration_seconds);
        clip.sample_rate = duration_seconds > 0.0 && sample_count > 1u
            ? static_cast<float>(static_cast<double>(sample_count - 1u) / duration_seconds)
            : kTargetSampleRate;
        clip.tracks.resize(skeleton.nodes.size());

        for (std::size_t bone = 0u; bone < skeleton.nodes.size(); ++bone) {
            Animation::Track& track = clip.tracks[bone];
            track.samples.reserve(sample_count);

            for (std::size_t sample = 0u; sample < sample_count; ++sample) {
                const double factor = sample_count > 1u
                    ? static_cast<double>(sample) / static_cast<double>(sample_count - 1u)
                    : 0.0;
                const double time = stack->time_begin + duration_seconds * factor;
                track.samples.push_back(toAnimationTransform(
                    ufbx_evaluate_transform(stack->anim, skeleton.nodes[bone], time)
                ));
            }
        }

        result.push_back(std::move(clip));
    }

    return result;
}

Animation::SkinWeights skinWeights(
    const ufbx_skin_deformer *skin,
    std::uint32_t vertex,
    const SkeletonBuild& skeleton)
{
    Animation::SkinWeights result;
    if (!skin || vertex >= skin->vertices.count) return result;

    struct Influence {
        float weight = 0.0f;
        std::uint16_t joint = 0u;
    };

    std::array<Influence, 4> strongest {};
    const ufbx_skin_vertex vertex_weights = skin->vertices.data[vertex];

    for (std::size_t index = 0u; index < vertex_weights.num_weights; ++index) {
        const ufbx_skin_weight weight = skin->weights.data[vertex_weights.weight_begin + index];
        if (weight.weight <= 0.0 || weight.cluster_index >= skin->clusters.count) continue;

        const ufbx_skin_cluster *cluster = skin->clusters.data[weight.cluster_index];
        if (!cluster || !cluster->bone_node) continue;

        const auto bone = skeleton.indices.find(cluster->bone_node);
        if (bone == skeleton.indices.end()) continue;

        Influence candidate {
            static_cast<float>(weight.weight),
            bone->second,
        };

        for (std::size_t slot = 0u; slot < strongest.size(); ++slot) {
            if (candidate.weight <= strongest[slot].weight) continue;
            for (std::size_t move = strongest.size() - 1u; move > slot; --move) {
                strongest[move] = strongest[move - 1u];
            }
            strongest[slot] = candidate;
            break;
        }
    }

    float total = 0.0f;
    for (const Influence& influence : strongest) total += influence.weight;
    if (total <= 1.0e-8f) return result;

    const float inverse_total = 1.0f / total;
    for (std::size_t index = 0u; index < strongest.size(); ++index) {
        result.joints[index] = strongest[index].joint;
        result.weights[index] = strongest[index].weight * inverse_total;
    }
    return result;
}

struct Builder {
    const ufbx_material *source_material = nullptr;
    MeshData mesh;
};

} // namespace

bool load(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) {
        if (error) *error = "null FBX destination";
        return false;
    }
    *document = {};

    ufbx_load_opts options{};
    options.generate_missing_normals = true;
    options.normalize_normals = true;
    options.ignore_missing_external_files = true;

    ufbx_error load_error{};
    ufbx_scene *scene = ufbx_load_file(path.c_str(), &options, &load_error);
    if (!scene) {
        if (error) {
            char buffer[1024]{};
            ufbx_format_error(buffer, sizeof(buffer), &load_error);
            *error = "cannot load FBX: " + path + ": " + buffer;
        }
        return false;
    }

    const std::filesystem::path source_path(path);
    const SkeletonBuild skeleton_build = collectSkeleton(scene);
    if (skeleton_build.overflow) {
        ufbx_free_scene(scene);
        if (error) *error = "FBX skeleton exceeds 65535 nodes: " + path;
        return false;
    }

    document->has_skeleton = !skeleton_build.nodes.empty();
    if (document->has_skeleton) {
        document->skeleton = makeSkeleton(skeleton_build, source_path);
        document->animations = makeAnimations(scene, skeleton_build);
    }

    for (std::size_t mesh_index = 0u; mesh_index < scene->meshes.count; ++mesh_index) {
        const ufbx_mesh *mesh = scene->meshes.data[mesh_index];
        if (!mesh || mesh->num_faces == 0u) continue;

        const ufbx_skin_deformer *skin = mesh->skin_deformers.count > 0u
            ? mesh->skin_deformers.data[0]
            : nullptr;

        const std::size_t triangle_index_capacity =
            std::max<std::size_t>(static_cast<std::size_t>(mesh->max_face_triangles) * 3u, 3u);
        std::vector<std::uint32_t> triangle_indices(triangle_index_capacity);

        for (std::size_t instance_index = 0u; instance_index < mesh->instances.count; ++instance_index) {
            const ufbx_node *node = mesh->instances.data[instance_index];
            if (!node || !node->visible) continue;

            const std::size_t material_count = std::max<std::size_t>(
                std::max(node->materials.count, mesh->materials.count),
                1u
            );
            std::vector<Builder> builders(material_count);
            for (std::size_t i = 0u; i < material_count; ++i) {
                builders[i].source_material = materialFor(
                    node,
                    mesh,
                    static_cast<std::uint32_t>(i)
                );
            }

            const ufbx_matrix normal_to_world = ufbx_matrix_for_normals(&node->geometry_to_world);

            for (std::size_t face_index = 0u; face_index < mesh->faces.count; ++face_index) {
                if (mesh->face_hole.count > face_index && mesh->face_hole.data[face_index]) continue;

                const ufbx_face face = mesh->faces.data[face_index];
                std::uint32_t material_index = 0u;
                if (mesh->face_material.count > face_index) {
                    material_index = mesh->face_material.data[face_index];
                }
                if (material_index >= builders.size()) material_index = 0u;
                Builder& builder = builders[material_index];

                const std::uint32_t triangle_index_count = ufbx_triangulate_face(
                    triangle_indices.data(),
                    triangle_indices.size(),
                    mesh,
                    face
                );

                for (std::uint32_t i = 0u; i < triangle_index_count; ++i) {
                    const std::uint32_t index = triangle_indices[i];
                    const ufbx_vec3 local_position = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                    const ufbx_vec3 local_normal = mesh->vertex_normal.exists
                        ? ufbx_get_vertex_vec3(&mesh->vertex_normal, index)
                        : ufbx_zero_vec3;
                    const ufbx_vec2 uv = mesh->vertex_uv.exists
                        ? ufbx_get_vertex_vec2(&mesh->vertex_uv, index)
                        : ufbx_zero_vec2;

                    const ufbx_vec3 world_position =
                        ufbx_transform_position(&node->geometry_to_world, local_position);
                    const ufbx_vec3 world_normal =
                        ufbx_transform_direction(&normal_to_world, local_normal);

                    Vertex vertex;
                    vertex.position = toVec3(world_position);
                    vertex.normal = normalize(toVec3(world_normal));
                    vertex.uv = toVec2(uv);

                    if (skin && index < mesh->vertex_indices.count) {
                        vertex.skin = skinWeights(
                            skin,
                            mesh->vertex_indices.data[index],
                            skeleton_build
                        );
                    }

                    builder.mesh.indices.push_back(
                        static_cast<std::uint32_t>(builder.mesh.vertices.size())
                    );
                    builder.mesh.vertices.push_back(vertex);
                }
            }

            for (Builder& builder : builders) {
                if (builder.mesh.indices.empty()) continue;
                builder.mesh.bounds = calculateBounds(builder.mesh.vertices);
                document->parts.push_back({
                    std::move(builder.mesh),
                    convertMaterial(builder.source_material, source_path),
                });
            }
        }
    }

    ufbx_free_scene(scene);

    if (document->parts.empty()) {
        if (error) *error = "FBX contains no renderable mesh instances: " + path;
        return false;
    }

    return true;
}

} // namespace Models::Fbx
