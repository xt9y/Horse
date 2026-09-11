#include "Models/Runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Models::Runtime {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

Quat normalized(Quat value)
{
    const float length2 = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (length2 <= 1.0e-20f) return {};
    const float inverse = 1.0f / std::sqrt(length2);
    value.x *= inverse;
    value.y *= inverse;
    value.z *= inverse;
    value.w *= inverse;
    return value;
}

Quat slerp(Quat a, Quat b, float factor)
{
    a = normalized(a);
    b = normalized(b);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        dot = -dot;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
    }
    if (dot > 0.9995f) {
        return normalized({
            a.x + (b.x - a.x) * factor,
            a.y + (b.y - a.y) * factor,
            a.z + (b.z - a.z) * factor,
            a.w + (b.w - a.w) * factor,
        });
    }
    const float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float sine = std::sin(theta);
    if (std::abs(sine) <= 1.0e-8f) return a;
    const float wa = std::sin((1.0f - factor) * theta) / sine;
    const float wb = std::sin(factor * theta) / sine;
    return normalized({
        a.x * wa + b.x * wb,
        a.y * wa + b.y * wb,
        a.z * wa + b.z * wb,
        a.w * wa + b.w * wb,
    });
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result {};
    for (std::size_t column = 0u; column < 4u; ++column) {
        for (std::size_t row = 0u; row < 4u; ++row) {
            float value = 0.0f;
            for (std::size_t inner = 0u; inner < 4u; ++inner)
                value += a[inner * 4u + row] * b[column * 4u + inner];
            result[column * 4u + row] = value;
        }
    }
    return result;
}

Mat4 compose(Vec3 translation, Quat rotation, Vec3 scale)
{
    rotation = normalized(rotation);
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    return {
        (1.0f - 2.0f * (yy + zz)) * scale.x,
        (2.0f * (xy + wz)) * scale.x,
        (2.0f * (xz - wy)) * scale.x,
        0.0f,
        (2.0f * (xy - wz)) * scale.y,
        (1.0f - 2.0f * (xx + zz)) * scale.y,
        (2.0f * (yz + wx)) * scale.y,
        0.0f,
        (2.0f * (xz + wy)) * scale.z,
        (2.0f * (yz - wx)) * scale.z,
        (1.0f - 2.0f * (xx + yy)) * scale.z,
        0.0f,
        translation.x,
        translation.y,
        translation.z,
        1.0f,
    };
}

Vec3 transformPoint(const Mat4& matrix, Vec3 value)
{
    return {
        matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12],
        matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13],
        matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14],
    };
}

Vec3 transformVector(const Mat4& matrix, Vec3 value)
{
    return {
        matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z,
        matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z,
        matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z,
    };
}

Vec3 normalized(Vec3 value)
{
    const float length2 = value.x * value.x + value.y * value.y + value.z * value.z;
    if (length2 <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    const float inverse = 1.0f / std::sqrt(length2);
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

bool inverse(const Mat4& source, Mat4 *output)
{
    if (!output) return false;
    double augmented[4][8] {};
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column)
            augmented[row][column] = static_cast<double>(source[column * 4u + row]);
        augmented[row][row + 4u] = 1.0;
    }

    for (std::size_t column = 0u; column < 4u; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1u; row < 4u; ++row)
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        if (std::abs(augmented[pivot][column]) <= 1.0e-12) return false;
        if (pivot != column)
            for (std::size_t entry = 0u; entry < 8u; ++entry)
                std::swap(augmented[pivot][entry], augmented[column][entry]);

        const double divisor = augmented[column][column];
        for (double& entry : augmented[column]) entry /= divisor;
        for (std::size_t row = 0u; row < 4u; ++row) {
            if (row == column) continue;
            const double factor = augmented[row][column];
            for (std::size_t entry = 0u; entry < 8u; ++entry)
                augmented[row][entry] -= factor * augmented[column][entry];
        }
    }

    for (std::size_t row = 0u; row < 4u; ++row)
        for (std::size_t column = 0u; column < 4u; ++column)
            (*output)[column * 4u + row] = static_cast<float>(augmented[row][column + 4u]);
    return true;
}

std::vector<float> defaultWeights(ModelHandle model, std::size_t node_index)
{
    if (const NodeData *source = node(model, node_index); source && !source->weights.empty()) return source->weights;
    for (std::size_t part_index = 0u; part_index < partCount(model); ++part_index) {
        const ModelPart *model_part = part(model, part_index);
        if (!model_part || model_part->node != node_index) continue;
        const MeshData *source = mesh(model_part->mesh);
        if (!source) continue;
        if (!source->morph_weights.empty()) return source->morph_weights;
        if (!source->morph_targets.empty()) return std::vector<float>(source->morph_targets.size(), 0.0f);
    }
    return {};
}

bool buildWorldNode(ModelHandle model, std::size_t index, Pose *pose, std::vector<std::uint8_t> *state, std::string *error)
{
    if (!pose || !state || index >= pose->nodes.size()) return fail(error, "invalid model runtime node");
    if ((*state)[index] == 2u) return true;
    if ((*state)[index] == 1u) return fail(error, "model node hierarchy contains a cycle");
    (*state)[index] = 1u;
    const NodeData *source = node(model, index);
    if (!source) return fail(error, "model runtime references invalid node");
    if (source->parent >= 0) {
        const std::size_t parent = static_cast<std::size_t>(source->parent);
        if (parent >= pose->nodes.size()) return fail(error, "model node parent is invalid");
        if (!buildWorldNode(model, parent, pose, state, error)) return false;
        pose->nodes[index].world = multiply(pose->nodes[parent].world, pose->nodes[index].local);
    } else {
        pose->nodes[index].world = pose->nodes[index].local;
    }
    (*state)[index] = 2u;
    return true;
}

bool buildWorld(ModelHandle model, Pose *pose, std::string *error)
{
    std::vector<std::uint8_t> state(pose ? pose->nodes.size() : 0u, 0u);
    for (std::size_t index = 0u; pose && index < pose->nodes.size(); ++index)
        if (!buildWorldNode(model, index, pose, &state, error)) return false;
    return pose != nullptr;
}

std::size_t outputWidth(const AnimationSamplerData& sampler)
{
    if (sampler.input.empty()) return 0u;
    const std::size_t multiplier = sampler.interpolation == AnimationInterpolation::CubicSpline ? 3u : 1u;
    if (sampler.input.size() > std::numeric_limits<std::size_t>::max() / multiplier) return 0u;
    const std::size_t rows = sampler.input.size() * multiplier;
    if (rows == 0u || sampler.output.size() % rows != 0u) return 0u;
    return sampler.output.size() / rows;
}

bool sampledVector(
    const AnimationSamplerData& sampler,
    float time,
    std::size_t width,
    bool quaternion,
    std::vector<float> *output,
    std::string *error)
{
    if (!output || sampler.input.empty() || width == 0u) return fail(error, "invalid model animation sampler");
    const std::size_t expected_width = outputWidth(sampler);
    if (expected_width != width) return fail(error, "model animation sampler output width mismatch");
    if (!std::is_sorted(sampler.input.begin(), sampler.input.end()))
        return fail(error, "model animation key times are not sorted");

    std::size_t first = 0u;
    std::size_t second = 0u;
    float factor = 0.0f;
    float delta = 0.0f;
    if (sampler.input.size() > 1u && time > sampler.input.front()) {
        if (time >= sampler.input.back()) {
            first = second = sampler.input.size() - 1u;
        } else {
            const auto upper = std::upper_bound(sampler.input.begin(), sampler.input.end(), time);
            second = static_cast<std::size_t>(upper - sampler.input.begin());
            first = second - 1u;
            delta = sampler.input[second] - sampler.input[first];
            factor = delta > 0.0f ? clamp01((time - sampler.input[first]) / delta) : 0.0f;
        }
    }

    const std::size_t slots = sampler.interpolation == AnimationInterpolation::CubicSpline ? 3u : 1u;
    const auto base = [&](std::size_t key, std::size_t slot) {
        return (key * slots + slot) * width;
    };
    output->assign(width, 0.0f);

    if (first == second || sampler.interpolation == AnimationInterpolation::Step) {
        const std::size_t offset = base(first, sampler.interpolation == AnimationInterpolation::CubicSpline ? 1u : 0u);
        std::copy_n(sampler.output.begin() + static_cast<std::ptrdiff_t>(offset), width, output->begin());
    } else if (sampler.interpolation == AnimationInterpolation::Linear) {
        if (quaternion && width == 4u) {
            const std::size_t a = base(first, 0u);
            const std::size_t b = base(second, 0u);
            const Quat result = slerp(
                {sampler.output[a], sampler.output[a + 1u], sampler.output[a + 2u], sampler.output[a + 3u]},
                {sampler.output[b], sampler.output[b + 1u], sampler.output[b + 2u], sampler.output[b + 3u]},
                factor
            );
            *output = {result.x, result.y, result.z, result.w};
        } else {
            const std::size_t a = base(first, 0u);
            const std::size_t b = base(second, 0u);
            for (std::size_t component = 0u; component < width; ++component)
                (*output)[component] = sampler.output[a + component] +
                    (sampler.output[b + component] - sampler.output[a + component]) * factor;
        }
    } else {
        const float t2 = factor * factor;
        const float t3 = t2 * factor;
        const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float h10 = t3 - 2.0f * t2 + factor;
        const float h01 = -2.0f * t3 + 3.0f * t2;
        const float h11 = t3 - t2;
        const std::size_t value0 = base(first, 1u);
        const std::size_t tangent0 = base(first, 2u);
        const std::size_t tangent1 = base(second, 0u);
        const std::size_t value1 = base(second, 1u);
        for (std::size_t component = 0u; component < width; ++component) {
            (*output)[component] = h00 * sampler.output[value0 + component] +
                h10 * delta * sampler.output[tangent0 + component] +
                h01 * sampler.output[value1 + component] +
                h11 * delta * sampler.output[tangent1 + component];
        }
        if (quaternion && width == 4u) {
            const Quat result = normalized({(*output)[0], (*output)[1], (*output)[2], (*output)[3]});
            *output = {result.x, result.y, result.z, result.w};
        }
    }

    if (quaternion && width == 4u && sampler.interpolation == AnimationInterpolation::Step) {
        const Quat result = normalized({(*output)[0], (*output)[1], (*output)[2], (*output)[3]});
        *output = {result.x, result.y, result.z, result.w};
    }
    return true;
}

void applyKnownPointer(Pose *pose, const std::string& pointer, const std::vector<float>& value)
{
    if (!pose || value.empty() || !pointer.starts_with("/nodes/")) return;
    const std::size_t begin = 7u;
    const std::size_t slash = pointer.find('/', begin);
    if (slash == std::string::npos) return;
    std::size_t index = 0u;
    for (std::size_t cursor = begin; cursor < slash; ++cursor) {
        const char character = pointer[cursor];
        if (character < '0' || character > '9') return;
        if (index > (std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(character - '0')) / 10u) return;
        index = index * 10u + static_cast<std::size_t>(character - '0');
    }
    if (index >= pose->nodes.size()) return;
    const std::string tail = pointer.substr(slash);
    const bool enabled = value[0] >= 0.5f;
    if (tail == "/extensions/KHR_node_visibility/visible") pose->nodes[index].visible = enabled;
    else if (tail == "/extensions/KHR_node_selectability/selectable") pose->nodes[index].selectable = enabled;
    else if (tail == "/extensions/KHR_node_hoverability/hoverable") pose->nodes[index].hoverable = enabled;
}

void calculateBounds(const std::vector<Vertex>& vertices, Bounds *bounds)
{
    if (!bounds || vertices.empty()) return;
    bounds->minimum = vertices.front().position;
    bounds->maximum = vertices.front().position;
    for (const Vertex& vertex : vertices) {
        bounds->minimum.x = std::min(bounds->minimum.x, vertex.position.x);
        bounds->minimum.y = std::min(bounds->minimum.y, vertex.position.y);
        bounds->minimum.z = std::min(bounds->minimum.z, vertex.position.z);
        bounds->maximum.x = std::max(bounds->maximum.x, vertex.position.x);
        bounds->maximum.y = std::max(bounds->maximum.y, vertex.position.y);
        bounds->maximum.z = std::max(bounds->maximum.z, vertex.position.z);
    }
}

std::array<std::uint16_t, 4> joints(const Joint4& value)
{
    return {value.x, value.y, value.z, value.w};
}

std::array<float, 4> weights(const Vec4& value)
{
    return {value.x, value.y, value.z, value.w};
}

} // namespace

bool reset(ModelHandle model, Pose *pose, std::string *error)
{
    if (error) error->clear();
    if (!pose || model == INVALID_MODEL) return fail(error, "invalid model runtime pose");
    pose->model = model;
    pose->animation = INVALID_INDEX;
    pose->time_seconds = 0.0f;
    pose->pointer_values.clear();
    pose->nodes.resize(nodeCount(model));
    for (std::size_t index = 0u; index < pose->nodes.size(); ++index) {
        const NodeData *source = node(model, index);
        if (!source) return fail(error, "model runtime node is unavailable");
        NodeState& destination = pose->nodes[index];
        destination.translation = source->translation;
        destination.rotation = normalized(source->rotation);
        destination.scale = source->scale;
        destination.weights = defaultWeights(model, index);
        destination.visible = source->visible;
        destination.selectable = source->selectable;
        destination.hoverable = source->hoverable;
        destination.local = source->has_matrix
            ? source->matrix
            : compose(destination.translation, destination.rotation, destination.scale);
    }
    return buildWorld(model, pose, error);
}

bool sample(
    ModelHandle model,
    std::size_t animation_index,
    float time_seconds,
    bool loop,
    Pose *pose,
    std::string *error)
{
    if (!reset(model, pose, error)) return false;
    const ModelAnimationData *animation = modelAnimation(model, animation_index);
    if (!animation) return fail(error, "model animation index is invalid");

    float time = time_seconds;
    if (animation->duration > 0.0f) {
        if (loop) {
            time = std::fmod(time, animation->duration);
            if (time < 0.0f) time += animation->duration;
        } else {
            time = std::clamp(time, 0.0f, animation->duration);
        }
    } else {
        time = 0.0f;
    }
    pose->animation = static_cast<std::uint32_t>(animation_index);
    pose->time_seconds = time;
    std::vector<std::uint8_t> changed(pose->nodes.size(), 0u);

    for (const AnimationChannelData& channel : animation->channels) {
        if (channel.sampler >= animation->samplers.size()) return fail(error, "model animation channel sampler is invalid");
        const AnimationSamplerData& sampler = animation->samplers[channel.sampler];
        std::size_t width = 0u;
        bool quaternion = false;
        switch (channel.path) {
            case AnimationPath::Translation:
            case AnimationPath::Scale:
                width = 3u;
                break;
            case AnimationPath::Rotation:
                width = 4u;
                quaternion = true;
                break;
            case AnimationPath::Weights:
            case AnimationPath::Pointer:
                width = outputWidth(sampler);
                break;
        }
        std::vector<float> value;
        if (!sampledVector(sampler, time, width, quaternion, &value, error)) return false;

        if (channel.path == AnimationPath::Pointer) {
            pose->pointer_values.insert_or_assign(channel.pointer, value);
            applyKnownPointer(pose, channel.pointer, value);
            continue;
        }
        if (channel.node == INVALID_INDEX || channel.node >= pose->nodes.size())
            return fail(error, "model animation channel node is invalid");
        NodeState& destination = pose->nodes[channel.node];
        if (channel.path == AnimationPath::Translation) {
            destination.translation = {value[0], value[1], value[2]};
            changed[channel.node] = 1u;
        } else if (channel.path == AnimationPath::Rotation) {
            destination.rotation = normalized({value[0], value[1], value[2], value[3]});
            changed[channel.node] = 1u;
        } else if (channel.path == AnimationPath::Scale) {
            destination.scale = {value[0], value[1], value[2]};
            changed[channel.node] = 1u;
        } else {
            destination.weights = std::move(value);
        }
    }

    for (std::size_t index = 0u; index < pose->nodes.size(); ++index) {
        const NodeData *source = node(model, index);
        if (!source) return fail(error, "model runtime node is unavailable");
        if (changed[index] != 0u || !source->has_matrix)
            pose->nodes[index].local = compose(
                pose->nodes[index].translation,
                pose->nodes[index].rotation,
                pose->nodes[index].scale
            );
    }
    return buildWorld(model, pose, error);
}

bool deformPart(
    ModelHandle model,
    std::size_t part_index,
    const Pose& pose,
    DeformedPart *output,
    std::string *error)
{
    if (error) error->clear();
    if (!output || pose.model != model) return fail(error, "model runtime pose does not belong to model");
    const ModelPart *model_part = part(model, part_index);
    if (!model_part) return fail(error, "model part index is invalid");
    const MeshData *source = mesh(model_part->mesh);
    if (!source) return fail(error, "model part mesh is invalid");

    output->primitive_mode = source->primitive_mode;
    output->vertices = source->vertices;
    output->indices = source->source_indices.empty() ? source->indices : source->source_indices;
    output->material = model_part->material;
    output->node_world = model_part->node != INVALID_INDEX && model_part->node < pose.nodes.size()
        ? pose.nodes[model_part->node].world
        : identityMatrix();

    std::vector<float> morph_weights = source->morph_weights;
    if (model_part->node != INVALID_INDEX && model_part->node < pose.nodes.size() && !pose.nodes[model_part->node].weights.empty())
        morph_weights = pose.nodes[model_part->node].weights;
    if (morph_weights.size() < source->morph_targets.size()) morph_weights.resize(source->morph_targets.size(), 0.0f);

    for (std::size_t target_index = 0u; target_index < source->morph_targets.size(); ++target_index) {
        const float weight = morph_weights[target_index];
        if (std::abs(weight) <= 1.0e-12f) continue;
        const MorphTargetData& target = source->morph_targets[target_index];
        if (!target.positions.empty() && target.positions.size() != output->vertices.size())
            return fail(error, "model morph POSITION count mismatch");
        if (!target.normals.empty() && target.normals.size() != output->vertices.size())
            return fail(error, "model morph NORMAL count mismatch");
        if (!target.tangents.empty() && target.tangents.size() != output->vertices.size())
            return fail(error, "model morph TANGENT count mismatch");
        for (std::size_t vertex = 0u; vertex < output->vertices.size(); ++vertex) {
            if (!target.positions.empty()) {
                output->vertices[vertex].position.x += target.positions[vertex].x * weight;
                output->vertices[vertex].position.y += target.positions[vertex].y * weight;
                output->vertices[vertex].position.z += target.positions[vertex].z * weight;
            }
            if (!target.normals.empty()) {
                output->vertices[vertex].normal.x += target.normals[vertex].x * weight;
                output->vertices[vertex].normal.y += target.normals[vertex].y * weight;
                output->vertices[vertex].normal.z += target.normals[vertex].z * weight;
            }
            if (!target.tangents.empty()) {
                output->vertices[vertex].tangent.x += target.tangents[vertex].x * weight;
                output->vertices[vertex].tangent.y += target.tangents[vertex].y * weight;
                output->vertices[vertex].tangent.z += target.tangents[vertex].z * weight;
            }
        }
    }

    const NodeData *part_node = model_part->node != INVALID_INDEX ? node(model, model_part->node) : nullptr;
    if (part_node && part_node->skin != INVALID_INDEX) {
        const SkinData *skin_data = skin(model, part_node->skin);
        if (!skin_data) return fail(error, "model node skin is invalid");
        Mat4 inverse_mesh;
        if (!inverse(output->node_world, &inverse_mesh)) return fail(error, "model skin mesh transform is singular");
        std::vector<Mat4> skin_matrices(skin_data->joints.size(), identityMatrix());
        for (std::size_t joint = 0u; joint < skin_data->joints.size(); ++joint) {
            const std::uint32_t node_index = skin_data->joints[joint];
            if (node_index >= pose.nodes.size()) return fail(error, "model skin joint node is invalid");
            const Mat4 inverse_bind = joint < skin_data->inverse_bind_matrices.size()
                ? skin_data->inverse_bind_matrices[joint]
                : identityMatrix();
            skin_matrices[joint] = multiply(inverse_mesh, multiply(pose.nodes[node_index].world, inverse_bind));
        }

        const std::size_t set_count = std::min(source->joint_sets.size(), source->weight_sets.size());
        for (std::size_t vertex_index = 0u; vertex_index < output->vertices.size(); ++vertex_index) {
            Vec3 position {};
            Vec3 normal {};
            Vec3 tangent {};
            float total = 0.0f;

            const auto accumulate = [&](std::uint16_t joint, float weight) -> bool {
                if (weight == 0.0f) return true;
                if (joint >= skin_matrices.size()) return false;
                const Vec3 p = transformPoint(skin_matrices[joint], output->vertices[vertex_index].position);
                const Vec3 n = transformVector(skin_matrices[joint], output->vertices[vertex_index].normal);
                const Vec3 t = transformVector(skin_matrices[joint], {
                    output->vertices[vertex_index].tangent.x,
                    output->vertices[vertex_index].tangent.y,
                    output->vertices[vertex_index].tangent.z,
                });
                position.x += p.x * weight; position.y += p.y * weight; position.z += p.z * weight;
                normal.x += n.x * weight; normal.y += n.y * weight; normal.z += n.z * weight;
                tangent.x += t.x * weight; tangent.y += t.y * weight; tangent.z += t.z * weight;
                total += weight;
                return true;
            };

            if (set_count != 0u) {
                for (std::size_t set = 0u; set < set_count; ++set) {
                    if (vertex_index >= source->joint_sets[set].size() || vertex_index >= source->weight_sets[set].size())
                        return fail(error, "model skin attribute set count mismatch");
                    const auto joint_values = joints(source->joint_sets[set][vertex_index]);
                    const auto weight_values = weights(source->weight_sets[set][vertex_index]);
                    for (std::size_t component = 0u; component < 4u; ++component)
                        if (!accumulate(joint_values[component], weight_values[component]))
                            return fail(error, "model skin vertex references invalid joint");
                }
            } else {
                for (std::size_t component = 0u; component < 4u; ++component)
                    if (!accumulate(
                            output->vertices[vertex_index].skin.joints[component],
                            output->vertices[vertex_index].skin.weights[component]))
                        return fail(error, "model skin vertex references invalid joint");
            }

            if (total > 1.0e-12f) {
                const float inverse_total = 1.0f / total;
                output->vertices[vertex_index].position = {
                    position.x * inverse_total,
                    position.y * inverse_total,
                    position.z * inverse_total,
                };
                output->vertices[vertex_index].normal = normalized(Vec3{
                    normal.x * inverse_total,
                    normal.y * inverse_total,
                    normal.z * inverse_total,
                });
                const Vec3 tangent_normalized = normalized(Vec3{
                    tangent.x * inverse_total,
                    tangent.y * inverse_total,
                    tangent.z * inverse_total,
                });
                output->vertices[vertex_index].tangent.x = tangent_normalized.x;
                output->vertices[vertex_index].tangent.y = tangent_normalized.y;
                output->vertices[vertex_index].tangent.z = tangent_normalized.z;
            }
        }
    } else {
        for (Vertex& vertex : output->vertices) {
            vertex.normal = normalized(vertex.normal);
            const Vec3 tangent = normalized(Vec3{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z});
            vertex.tangent.x = tangent.x;
            vertex.tangent.y = tangent.y;
            vertex.tangent.z = tangent.z;
        }
    }

    calculateBounds(output->vertices, &output->bounds);
    return true;
}

bool instanceMatrices(
    ModelHandle model,
    std::size_t instance_index,
    const Pose& pose,
    std::vector<Mat4> *matrices,
    std::string *error)
{
    if (error) error->clear();
    if (!matrices || pose.model != model) return fail(error, "invalid model instance runtime output");
    const InstanceData *source = instance(model, instance_index);
    if (!source) return fail(error, "model instance index is invalid");
    std::size_t count = std::max({source->translations.size(), source->rotations.size(), source->scales.size()});
    for (const auto& [semantic, attribute] : source->attributes) {
        (void)semantic;
        if (attribute.components != 0u) count = std::max(count, attribute.values.size() / attribute.components);
    }
    matrices->clear();
    matrices->reserve(count);
    const Mat4 base = source->node != INVALID_INDEX && source->node < pose.nodes.size()
        ? pose.nodes[source->node].world
        : identityMatrix();
    for (std::size_t index = 0u; index < count; ++index) {
        const Vec3 translation = index < source->translations.size() ? source->translations[index] : Vec3{};
        const Quat rotation = index < source->rotations.size() ? source->rotations[index] : Quat{};
        const Vec3 scale = index < source->scales.size() ? source->scales[index] : Vec3{1.0f, 1.0f, 1.0f};
        matrices->push_back(multiply(base, compose(translation, rotation, scale)));
    }
    return true;
}

MaterialHandle materialForVariant(ModelHandle model, std::size_t part_index, std::uint32_t variant_index)
{
    const ModelPart *model_part = part(model, part_index);
    if (!model_part) return INVALID_MATERIAL;
    if (variant_index == INVALID_INDEX) return model_part->material;
    for (std::size_t mapping_index = 0u; mapping_index < variantMappingCount(model); ++mapping_index) {
        const VariantMappingData *mapping = variantMapping(model, mapping_index);
        if (!mapping || mapping->part != part_index) continue;
        if (std::find(mapping->variants.begin(), mapping->variants.end(), variant_index) != mapping->variants.end())
            return mapping->material;
    }
    return model_part->material;
}

} // namespace Models::Runtime
