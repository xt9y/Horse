#include "Models/Formats/Fbx.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Formats/FbxParser.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Models::Fbx {
namespace {

using RawDocument = FbxParser::Document;
using RawNode = FbxParser::Node;
using ObjectId = std::int64_t;
using Matrix = Animation::Mat4;

constexpr float kPi = 3.14159265358979323846f;
constexpr double kFbxTicksPerSecond = 46186158000.0;
constexpr float kTargetSampleRate = 30.0f;
constexpr std::size_t kMaximumClipSamples = 18000u;

struct Object {
    ObjectId id = 0;
    std::string kind;
    std::string name;
    std::string subtype;
    const RawNode *node = nullptr;
};

struct Connection {
    std::string type;
    ObjectId source = 0;
    ObjectId destination = 0;
    std::string property;
};

struct Scene {
    const RawDocument *document = nullptr;
    std::unordered_map<ObjectId, Object> objects;
    std::vector<ObjectId> object_order;
    std::vector<Connection> connections;
    std::unordered_map<ObjectId, std::vector<std::size_t>> outgoing;
    std::unordered_map<ObjectId, std::vector<std::size_t>> incoming;
};

struct Trs {
    Animation::Vec3 translation{};
    Animation::Vec3 rotation_degrees{};
    Animation::Vec3 scale{1.0f, 1.0f, 1.0f};
    Animation::Vec3 pre_rotation{};
    Animation::Vec3 post_rotation{};
};

struct PolygonCorner {
    std::uint32_t control = 0u;
    std::size_t polygon_vertex = 0u;
};

struct Polygon {
    std::vector<PolygonCorner> corners;
    std::size_t index = 0u;
};

struct LayerElement {
    std::string mapping = "ByPolygonVertex";
    std::string reference = "Direct";
    std::vector<double> direct;
    std::vector<std::int32_t> indices;
    std::size_t tuple_size = 0u;
};

struct Curve {
    std::vector<std::int64_t> times;
    std::vector<double> values;
};

struct SkeletonBuild {
    std::vector<ObjectId> models;
    std::unordered_map<ObjectId, std::uint16_t> indices;
};

std::string cleanName(std::string value)
{
    const std::size_t nul = value.find('\0');
    if (nul != std::string::npos) value.resize(nul);
    const std::size_t separator = value.rfind("::");
    if (separator != std::string::npos) value = value.substr(separator + 2u);
    return value;
}

const RawNode *child(const RawNode *node, const std::string& name)
{
    return node ? node->child(name) : nullptr;
}

std::vector<double> numericArray(const RawNode *node)
{
    return node ? node->numericArray() : std::vector<double>{};
}

std::vector<std::int32_t> int32Array(const RawNode *node)
{
    if (!node || node->properties.empty()) return {};
    if (const auto *values = node->properties[0].asInt32Array()) return *values;
    if (const auto *values64 = node->properties[0].asInt64Array()) {
        std::vector<std::int32_t> out;
        out.reserve(values64->size());
        for (std::int64_t value : *values64) out.push_back(static_cast<std::int32_t>(value));
        return out;
    }
    const std::vector<double> values = node->numericArray();
    std::vector<std::int32_t> out;
    out.reserve(values.size());
    for (double value : values) out.push_back(static_cast<std::int32_t>(value));
    return out;
}

std::vector<std::int64_t> int64Array(const RawNode *node)
{
    if (!node || node->properties.empty()) return {};
    if (const auto *values = node->properties[0].asInt64Array()) return *values;
    if (const auto *values32 = node->properties[0].asInt32Array()) {
        return std::vector<std::int64_t>(values32->begin(), values32->end());
    }
    const std::vector<double> values = node->numericArray();
    std::vector<std::int64_t> out;
    out.reserve(values.size());
    for (double value : values) out.push_back(static_cast<std::int64_t>(value));
    return out;
}

const RawNode *properties70(const Object& object)
{
    return child(object.node, "Properties70");
}

const RawNode *propertyNode(const Object& object, const std::string& name)
{
    const RawNode *properties = properties70(object);
    if (!properties) return nullptr;
    for (const RawNode& property : properties->children) {
        if (property.name != "P" || property.properties.empty()) continue;
        if (property.properties[0].asString() == name) return &property;
    }
    return nullptr;
}

std::vector<double> propertyValues(const Object& object, const std::string& name)
{
    const RawNode *property = propertyNode(object, name);
    if (!property || property->properties.size() <= 4u) return {};
    std::vector<double> result;
    result.reserve(property->properties.size() - 4u);
    for (std::size_t index = 4u; index < property->properties.size(); ++index) {
        result.push_back(property->properties[index].asDouble());
    }
    return result;
}

double propertyScalar(const Object& object, const std::string& name, double fallback)
{
    const std::vector<double> values = propertyValues(object, name);
    return values.empty() ? fallback : values[0];
}

Animation::Vec3 propertyVec3(
    const Object& object,
    const std::string& name,
    Animation::Vec3 fallback)
{
    const std::vector<double> values = propertyValues(object, name);
    if (values.size() < 3u) return fallback;
    return {
        static_cast<float>(values[0]),
        static_cast<float>(values[1]),
        static_cast<float>(values[2]),
    };
}

Trs objectTrs(const Object& object)
{
    Trs result;
    result.translation = propertyVec3(object, "Lcl Translation", result.translation);
    result.rotation_degrees = propertyVec3(object, "Lcl Rotation", result.rotation_degrees);
    result.scale = propertyVec3(object, "Lcl Scaling", result.scale);
    result.pre_rotation = propertyVec3(object, "PreRotation", result.pre_rotation);
    result.post_rotation = propertyVec3(object, "PostRotation", result.post_rotation);
    return result;
}

Animation::Quat quatMultiply(Animation::Quat a, Animation::Quat b)
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

Animation::Quat quatAxis(float x, float y, float z, float degrees)
{
    const float half = degrees * (kPi / 360.0f);
    const float sine = std::sin(half);
    return {x * sine, y * sine, z * sine, std::cos(half)};
}

Animation::Quat eulerQuaternion(Animation::Vec3 degrees)
{
    const Animation::Quat qx = quatAxis(1.0f, 0.0f, 0.0f, degrees.x);
    const Animation::Quat qy = quatAxis(0.0f, 1.0f, 0.0f, degrees.y);
    const Animation::Quat qz = quatAxis(0.0f, 0.0f, 1.0f, degrees.z);
    return quatMultiply(quatMultiply(qz, qy), qx);
}

Animation::Transform animationTransform(const Trs& trs)
{
    Animation::Vec3 combined_rotation {
        trs.rotation_degrees.x + trs.pre_rotation.x - trs.post_rotation.x,
        trs.rotation_degrees.y + trs.pre_rotation.y - trs.post_rotation.y,
        trs.rotation_degrees.z + trs.pre_rotation.z - trs.post_rotation.z,
    };
    return {trs.translation, eulerQuaternion(combined_rotation), trs.scale};
}

Matrix identityMatrix()
{
    return {};
}

Matrix matrixMultiply(const Matrix& a, const Matrix& b)
{
    return Animation::multiply(a, b);
}

Matrix matrixFromTrs(const Trs& trs)
{
    return Animation::matrix(animationTransform(trs));
}

Matrix matrixFromFbxArray(const std::vector<double>& values)
{
    Matrix result;
    if (values.size() < 16u) return result;
    // FBX serializes matrices row-major. Animation::Mat4 is column-major.
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            result.value[column * 4u + row] = static_cast<float>(values[row * 4u + column]);
        }
    }
    return result;
}

bool invertMatrix(const Matrix& input, Matrix *out)
{
    if (!out) return false;
    double augmented[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] = input.value[column * 4 + row];
        }
        augmented[row][4 + row] = 1.0;
    }

    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        }
        if (std::abs(augmented[pivot][column]) <= 1.0e-12) return false;
        if (pivot != column) {
            for (int item = 0; item < 8; ++item) std::swap(augmented[pivot][item], augmented[column][item]);
        }
        const double inverse = 1.0 / augmented[column][column];
        for (int item = 0; item < 8; ++item) augmented[column][item] *= inverse;
        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = augmented[row][column];
            for (int item = 0; item < 8; ++item) augmented[row][item] -= factor * augmented[column][item];
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            out->value[column * 4 + row] = static_cast<float>(augmented[row][4 + column]);
        }
    }
    return true;
}

Vec3 transformPoint(const Matrix& matrix, Vec3 point)
{
    const Animation::Vec3 value = Animation::transformPoint(matrix, {point.x, point.y, point.z});
    return {value.x, value.y, value.z};
}

Vec3 transformNormal(const Matrix& inverse_matrix, Vec3 normal)
{
    Vec3 result {
        inverse_matrix.value[0] * normal.x + inverse_matrix.value[1] * normal.y + inverse_matrix.value[2] * normal.z,
        inverse_matrix.value[4] * normal.x + inverse_matrix.value[5] * normal.y + inverse_matrix.value[6] * normal.z,
        inverse_matrix.value[8] * normal.x + inverse_matrix.value[9] * normal.y + inverse_matrix.value[10] * normal.z,
    };
    const float length_squared = result.x * result.x + result.y * result.y + result.z * result.z;
    if (length_squared > 1.0e-20f) {
        const float inv = 1.0f / std::sqrt(length_squared);
        result.x *= inv;
        result.y *= inv;
        result.z *= inv;
    }
    return result;
}

Vec3 normalize(Vec3 value)
{
    const float squared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (squared <= 1.0e-20f) return {0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / std::sqrt(squared);
    return {value.x * inv, value.y * inv, value.z * inv};
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

Bounds calculateBounds(const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return {};
    const float maximum = std::numeric_limits<float>::max();
    Bounds bounds{{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
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

bool buildScene(const RawDocument& document, Scene *scene, std::string *error)
{
    if (!scene) return false;
    scene->document = &document;
    const RawNode *objects = document.root.child("Objects");
    if (!objects) {
        if (error) *error = "FBX has no Objects section";
        return false;
    }

    for (const RawNode& node : objects->children) {
        if (node.properties.empty()) continue;
        Object object;
        object.id = node.properties[0].asInt64();
        object.kind = node.name;
        object.name = node.properties.size() > 1u ? cleanName(node.properties[1].asString()) : std::string{};
        object.subtype = node.properties.size() > 2u ? node.properties[2].asString() : std::string{};
        object.node = &node;
        scene->object_order.push_back(object.id);
        scene->objects[object.id] = std::move(object);
    }

    const RawNode *connections = document.root.child("Connections");
    if (connections) {
        for (const RawNode& raw : connections->children) {
            if (raw.name != "C" || raw.properties.size() < 3u) continue;
            Connection connection;
            connection.type = raw.properties[0].asString();
            connection.source = raw.properties[1].asInt64();
            connection.destination = raw.properties[2].asInt64();
            if (raw.properties.size() > 3u) connection.property = raw.properties[3].asString();
            const std::size_t index = scene->connections.size();
            scene->connections.push_back(connection);
            scene->outgoing[connection.source].push_back(index);
            scene->incoming[connection.destination].push_back(index);
        }
    }
    return true;
}

const Object *object(const Scene& scene, ObjectId id)
{
    const auto found = scene.objects.find(id);
    return found == scene.objects.end() ? nullptr : &found->second;
}

ObjectId parentModel(const Scene& scene, ObjectId model)
{
    const auto found = scene.outgoing.find(model);
    if (found == scene.outgoing.end()) return 0;
    for (std::size_t connection_index : found->second) {
        const Connection& connection = scene.connections[connection_index];
        if (connection.type != "OO") continue;
        const Object *destination = object(scene, connection.destination);
        if (destination && destination->kind == "Model") return destination->id;
    }
    return 0;
}

Matrix globalModelMatrix(
    const Scene& scene,
    ObjectId model,
    std::unordered_map<ObjectId, Matrix>& cache,
    std::unordered_set<ObjectId>& visiting)
{
    const auto existing = cache.find(model);
    if (existing != cache.end()) return existing->second;
    const Object *model_object = object(scene, model);
    if (!model_object || model_object->kind != "Model" || !visiting.insert(model).second) {
        return identityMatrix();
    }
    const Matrix local = matrixFromTrs(objectTrs(*model_object));
    const ObjectId parent = parentModel(scene, model);
    const Matrix global = parent != 0
        ? matrixMultiply(globalModelMatrix(scene, parent, cache, visiting), local)
        : local;
    visiting.erase(model);
    cache[model] = global;
    return global;
}

std::vector<ObjectId> modelMaterials(const Scene& scene, ObjectId model)
{
    std::vector<ObjectId> result;
    const auto found = scene.incoming.find(model);
    if (found == scene.incoming.end()) return result;
    for (std::size_t connection_index : found->second) {
        const Connection& connection = scene.connections[connection_index];
        if (connection.type != "OO") continue;
        const Object *source = object(scene, connection.source);
        if (source && source->kind == "Material") result.push_back(source->id);
    }
    return result;
}

ObjectId geometryModel(const Scene& scene, ObjectId geometry)
{
    const auto found = scene.outgoing.find(geometry);
    if (found == scene.outgoing.end()) return 0;
    for (std::size_t index : found->second) {
        const Connection& connection = scene.connections[index];
        if (connection.type != "OO") continue;
        const Object *destination = object(scene, connection.destination);
        if (destination && destination->kind == "Model") return destination->id;
    }
    return 0;
}

std::vector<ObjectId> connectedSources(const Scene& scene, ObjectId destination, const std::string& kind)
{
    std::vector<ObjectId> result;
    const auto found = scene.incoming.find(destination);
    if (found == scene.incoming.end()) return result;
    for (std::size_t index : found->second) {
        const Connection& connection = scene.connections[index];
        const Object *source = object(scene, connection.source);
        if (source && source->kind == kind) result.push_back(source->id);
    }
    return result;
}

ObjectId skinForGeometry(const Scene& scene, ObjectId geometry)
{
    for (ObjectId id : connectedSources(scene, geometry, "Deformer")) {
        const Object *deformer = object(scene, id);
        if (deformer && deformer->subtype == "Skin") return id;
    }
    return 0;
}

std::vector<ObjectId> clustersForSkin(const Scene& scene, ObjectId skin)
{
    std::vector<ObjectId> result;
    for (ObjectId id : connectedSources(scene, skin, "Deformer")) {
        const Object *deformer = object(scene, id);
        if (deformer && deformer->subtype == "Cluster") result.push_back(id);
    }
    return result;
}

ObjectId boneForCluster(const Scene& scene, ObjectId cluster)
{
    for (ObjectId id : connectedSources(scene, cluster, "Model")) return id;
    return 0;
}

void addBoneWithParents(const Scene& scene, ObjectId model, SkeletonBuild *build)
{
    if (!build || model == 0 || build->indices.find(model) != build->indices.end()) return;
    const Object *bone = object(scene, model);
    if (!bone || bone->kind != "Model") return;
    const ObjectId parent = parentModel(scene, model);
    if (parent != 0) addBoneWithParents(scene, parent, build);
    if (build->models.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) return;
    const std::uint16_t index = static_cast<std::uint16_t>(build->models.size());
    build->models.push_back(model);
    build->indices.emplace(model, index);
}

SkeletonBuild collectSkeleton(const Scene& scene)
{
    SkeletonBuild result;
    for (ObjectId id : scene.object_order) {
        const Object *geometry = object(scene, id);
        if (!geometry || geometry->kind != "Geometry" || geometry->subtype != "Mesh") continue;
        const ObjectId skin = skinForGeometry(scene, id);
        if (skin == 0) continue;
        for (ObjectId cluster : clustersForSkin(scene, skin)) {
            addBoneWithParents(scene, boneForCluster(scene, cluster), &result);
        }
    }
    return result;
}

Animation::Skeleton makeSkeleton(const Scene& scene, const SkeletonBuild& build, const std::string& name)
{
    Animation::Skeleton result;
    result.name = name;
    result.bones.reserve(build.models.size());
    std::unordered_map<ObjectId, Matrix> global_cache;
    std::unordered_set<ObjectId> visiting;

    for (ObjectId id : build.models) {
        const Object *model = object(scene, id);
        Animation::Bone bone;
        bone.name = model ? model->name : std::string{};
        bone.bind_local = model ? animationTransform(objectTrs(*model)) : Animation::Transform{};
        const ObjectId parent = parentModel(scene, id);
        const auto parent_index = build.indices.find(parent);
        if (parent_index != build.indices.end()) bone.parent = static_cast<std::int32_t>(parent_index->second);

        const Matrix global = globalModelMatrix(scene, id, global_cache, visiting);
        Matrix inverse;
        if (invertMatrix(global, &inverse)) bone.inverse_bind = inverse;
        result.bones.push_back(std::move(bone));
    }
    return result;
}

LayerElement parseLayerElement(
    const RawNode *node,
    const std::string& direct_name,
    const std::string& index_name,
    std::size_t tuple_size)
{
    LayerElement result;
    result.tuple_size = tuple_size;
    if (!node) return result;
    if (const RawNode *mapping = child(node, "MappingInformationType")) {
        if (!mapping->properties.empty()) result.mapping = mapping->properties[0].asString(result.mapping);
    }
    if (const RawNode *reference = child(node, "ReferenceInformationType")) {
        if (!reference->properties.empty()) result.reference = reference->properties[0].asString(result.reference);
    }
    result.direct = numericArray(child(node, direct_name));
    result.indices = int32Array(child(node, index_name));
    return result;
}

std::size_t mappedElementIndex(
    const LayerElement& layer,
    std::size_t polygon,
    std::size_t polygon_vertex,
    std::uint32_t control)
{
    std::size_t mapped = polygon_vertex;
    if (layer.mapping == "ByVertice" || layer.mapping == "ByVertex") mapped = control;
    else if (layer.mapping == "ByPolygon") mapped = polygon;
    else if (layer.mapping == "AllSame") mapped = 0u;

    if (layer.reference == "IndexToDirect" || layer.reference == "Index") {
        if (mapped >= layer.indices.size() || layer.indices[mapped] < 0) return std::numeric_limits<std::size_t>::max();
        return static_cast<std::size_t>(layer.indices[mapped]);
    }
    return mapped;
}

Vec3 layerVec3(
    const LayerElement& layer,
    std::size_t polygon,
    std::size_t polygon_vertex,
    std::uint32_t control,
    Vec3 fallback)
{
    if (layer.tuple_size < 3u) return fallback;
    const std::size_t index = mappedElementIndex(layer, polygon, polygon_vertex, control);
    if (index == std::numeric_limits<std::size_t>::max()) return fallback;
    const std::size_t offset = index * layer.tuple_size;
    if (offset + 2u >= layer.direct.size()) return fallback;
    return normalize({
        static_cast<float>(layer.direct[offset]),
        static_cast<float>(layer.direct[offset + 1u]),
        static_cast<float>(layer.direct[offset + 2u]),
    });
}

Vec2 layerVec2(
    const LayerElement& layer,
    std::size_t polygon,
    std::size_t polygon_vertex,
    std::uint32_t control)
{
    if (layer.tuple_size < 2u) return {};
    const std::size_t index = mappedElementIndex(layer, polygon, polygon_vertex, control);
    if (index == std::numeric_limits<std::size_t>::max()) return {};
    const std::size_t offset = index * layer.tuple_size;
    if (offset + 1u >= layer.direct.size()) return {};
    return {
        static_cast<float>(layer.direct[offset]),
        static_cast<float>(layer.direct[offset + 1u]),
    };
}

int polygonMaterial(const LayerElement& layer, std::size_t polygon)
{
    if (layer.direct.empty() && layer.indices.empty()) return 0;
    std::size_t mapped = layer.mapping == "AllSame" ? 0u : polygon;
    if (layer.mapping == "ByPolygonVertex" || layer.mapping == "ByVertice" || layer.mapping == "ByVertex") {
        mapped = polygon;
    }
    if (layer.reference == "IndexToDirect" || layer.reference == "Index") {
        if (mapped < layer.indices.size()) return std::max(layer.indices[mapped], 0);
    }
    if (mapped < layer.direct.size()) return std::max(static_cast<int>(layer.direct[mapped]), 0);
    return 0;
}

std::vector<Polygon> parsePolygons(const RawNode& geometry, std::size_t control_count)
{
    const std::vector<std::int32_t> raw_indices = int32Array(child(&geometry, "PolygonVertexIndex"));
    std::vector<Polygon> result;
    Polygon current;
    std::size_t polygon_vertex = 0u;

    for (std::int32_t raw : raw_indices) {
        const bool last = raw < 0;
        const std::int64_t decoded = last ? -static_cast<std::int64_t>(raw) - 1ll : raw;
        if (decoded >= 0 && static_cast<std::size_t>(decoded) < control_count) {
            current.corners.push_back({static_cast<std::uint32_t>(decoded), polygon_vertex});
        }
        ++polygon_vertex;
        if (last) {
            current.index = result.size();
            if (current.corners.size() >= 3u) result.push_back(std::move(current));
            current = {};
        }
    }
    return result;
}

std::vector<Animation::SkinWeights> buildControlWeights(
    const Scene& scene,
    ObjectId skin,
    const SkeletonBuild& skeleton,
    std::size_t control_count)
{
    std::vector<Animation::SkinWeights> result(control_count);
    struct Influence { float weight; std::uint16_t joint; };
    std::vector<std::vector<Influence>> influences(control_count);

    for (ObjectId cluster_id : clustersForSkin(scene, skin)) {
        const Object *cluster = object(scene, cluster_id);
        if (!cluster) continue;
        const ObjectId bone_id = boneForCluster(scene, cluster_id);
        const auto bone = skeleton.indices.find(bone_id);
        if (bone == skeleton.indices.end()) continue;
        const std::vector<std::int32_t> indexes = int32Array(child(cluster->node, "Indexes"));
        const std::vector<double> weights = numericArray(child(cluster->node, "Weights"));
        const std::size_t count = std::min(indexes.size(), weights.size());
        for (std::size_t index = 0u; index < count; ++index) {
            if (indexes[index] < 0 || static_cast<std::size_t>(indexes[index]) >= control_count || weights[index] <= 0.0) continue;
            influences[static_cast<std::size_t>(indexes[index])].push_back({
                static_cast<float>(weights[index]), bone->second
            });
        }
    }

    for (std::size_t control = 0u; control < influences.size(); ++control) {
        auto& values = influences[control];
        std::sort(values.begin(), values.end(), [](const Influence& a, const Influence& b) {
            return a.weight > b.weight;
        });
        float total = 0.0f;
        const std::size_t count = std::min<std::size_t>(values.size(), 4u);
        for (std::size_t index = 0u; index < count; ++index) total += values[index].weight;
        if (total <= 1.0e-8f) continue;
        for (std::size_t index = 0u; index < count; ++index) {
            result[control].joints[index] = values[index].joint;
            result[control].weights[index] = values[index].weight / total;
        }
    }
    return result;
}

std::vector<Matrix> buildMeshBindPalette(
    const Scene& scene,
    ObjectId skin,
    const SkeletonBuild& skeleton)
{
    std::vector<Matrix> palette(skeleton.models.size());
    for (ObjectId cluster_id : clustersForSkin(scene, skin)) {
        const Object *cluster = object(scene, cluster_id);
        if (!cluster) continue;
        const auto bone = skeleton.indices.find(boneForCluster(scene, cluster_id));
        if (bone == skeleton.indices.end()) continue;
        const Matrix transform = matrixFromFbxArray(numericArray(child(cluster->node, "Transform")));
        const Matrix transform_link = matrixFromFbxArray(numericArray(child(cluster->node, "TransformLink")));
        Matrix inverse_link;
        if (!invertMatrix(transform_link, &inverse_link)) continue;
        palette[bone->second] = matrixMultiply(inverse_link, transform);
    }
    return palette;
}

ObjectId materialTexture(const Scene& scene, ObjectId material)
{
    const auto found = scene.incoming.find(material);
    if (found == scene.incoming.end()) return 0;
    ObjectId fallback = 0;
    for (std::size_t index : found->second) {
        const Connection& connection = scene.connections[index];
        const Object *source = object(scene, connection.source);
        if (!source || source->kind != "Texture") continue;
        if (fallback == 0) fallback = source->id;
        if (
            connection.property.find("Diffuse") != std::string::npos ||
            connection.property.find("BaseColor") != std::string::npos)
        {
            return source->id;
        }
    }
    return fallback;
}

std::string textureFilename(const Object& texture)
{
    const RawNode *relative = child(texture.node, "RelativeFilename");
    if (relative && !relative->properties.empty()) return relative->properties[0].asString();
    const RawNode *filename = child(texture.node, "FileName");
    if (filename && !filename->properties.empty()) return filename->properties[0].asString();
    return {};
}

const FbxParser::Bytes *embeddedTexture(const Scene& scene, ObjectId texture_id)
{
    const auto found = scene.incoming.find(texture_id);
    if (found == scene.incoming.end()) return nullptr;
    for (std::size_t index : found->second) {
        const Object *source = object(scene, scene.connections[index].source);
        if (!source || source->kind != "Video") continue;
        const RawNode *content = child(source->node, "Content");
        if (!content || content->properties.empty()) continue;
        if (const auto *bytes = std::get_if<FbxParser::Bytes>(&content->properties[0].value)) return bytes;
    }
    return nullptr;
}

MaterialData convertMaterial(
    const Scene& scene,
    ObjectId material_id,
    const std::filesystem::path& source_path)
{
    MaterialData result;
    const Object *material = object(scene, material_id);
    if (!material) return result;
    result.name = material->name;
    const Animation::Vec3 diffuse = propertyVec3(*material, "DiffuseColor", {1.0f, 1.0f, 1.0f});
    result.color = {diffuse.x, diffuse.y, diffuse.z};
    const double transparency = std::clamp(propertyScalar(*material, "TransparencyFactor", 0.0), 0.0, 1.0);
    const double opacity = propertyScalar(*material, "Opacity", 1.0 - transparency);
    result.opacity = std::clamp(static_cast<float>(opacity), 0.0f, 1.0f);

    const ObjectId texture_id = materialTexture(scene, material_id);
    const Object *texture = object(scene, texture_id);
    if (!texture) return result;

    if (const FbxParser::Bytes *content = embeddedTexture(scene, texture_id); content && !content->empty()) {
        const std::string cache_key = source_path.string() + "#" + texture->name;
        std::string ignored;
        result.diffuse_texture = loadTextureMemory(cache_key, content->data(), content->size(), &ignored);
        if (result.diffuse_texture != INVALID_TEXTURE) {
            result.texture_path = cache_key;
            return result;
        }
    }

    std::string filename = textureFilename(*texture);
    std::replace(filename.begin(), filename.end(), '\\', '/');
    if (!filename.empty()) {
        std::filesystem::path texture_path(filename);
        if (texture_path.is_relative()) texture_path = source_path.parent_path() / texture_path;
        texture_path = texture_path.lexically_normal();
        result.texture_path = texture_path.string();
        std::string ignored;
        result.diffuse_texture = loadTexture(result.texture_path, &ignored);
    }
    return result;
}

Curve parseCurve(const Object& curve)
{
    Curve result;
    result.times = int64Array(child(curve.node, "KeyTime"));
    const RawNode *float_values = child(curve.node, "KeyValueFloat");
    const RawNode *double_values = child(curve.node, "KeyValueDouble");
    result.values = numericArray(float_values ? float_values : double_values);
    const std::size_t count = std::min(result.times.size(), result.values.size());
    result.times.resize(count);
    result.values.resize(count);
    return result;
}

double sampleCurve(const Curve& curve, std::int64_t time, double fallback)
{
    if (curve.times.empty()) return fallback;
    if (time <= curve.times.front()) return curve.values.front();
    if (time >= curve.times.back()) return curve.values.back();
    const auto upper = std::upper_bound(curve.times.begin(), curve.times.end(), time);
    const std::size_t second = static_cast<std::size_t>(upper - curve.times.begin());
    const std::size_t first = second - 1u;
    const double span = static_cast<double>(curve.times[second] - curve.times[first]);
    const double factor = span > 0.0 ? static_cast<double>(time - curve.times[first]) / span : 0.0;
    return curve.values[first] + (curve.values[second] - curve.values[first]) * factor;
}

std::vector<Animation::AnimationClip> makeAnimations(const Scene& scene, const SkeletonBuild& skeleton)
{
    std::vector<Animation::AnimationClip> result;
    if (skeleton.models.empty()) return result;

    struct NodeCurves {
        ObjectId model = 0;
        std::string property;
        std::array<Curve, 3> axes;
        std::array<bool, 3> present{};
    };

    for (ObjectId stack_id : scene.object_order) {
        const Object *stack = object(scene, stack_id);
        if (!stack || stack->kind != "AnimationStack") continue;

        std::unordered_set<ObjectId> layers;
        for (ObjectId layer : connectedSources(scene, stack_id, "AnimationLayer")) layers.insert(layer);
        std::vector<NodeCurves> animated_nodes;
        std::int64_t minimum_time = std::numeric_limits<std::int64_t>::max();
        std::int64_t maximum_time = std::numeric_limits<std::int64_t>::min();

        for (ObjectId node_id : scene.object_order) {
            const Object *curve_node = object(scene, node_id);
            if (!curve_node || curve_node->kind != "AnimationCurveNode") continue;

            bool belongs = false;
            const auto node_out = scene.outgoing.find(node_id);
            if (node_out != scene.outgoing.end()) {
                for (std::size_t connection_index : node_out->second) {
                    const Connection& connection = scene.connections[connection_index];
                    if (layers.find(connection.destination) != layers.end()) belongs = true;
                }
            }
            if (!belongs) continue;

            NodeCurves entry;
            if (node_out != scene.outgoing.end()) {
                for (std::size_t connection_index : node_out->second) {
                    const Connection& connection = scene.connections[connection_index];
                    if (connection.type != "OP") continue;
                    const Object *destination = object(scene, connection.destination);
                    if (!destination || destination->kind != "Model") continue;
                    entry.model = destination->id;
                    entry.property = connection.property;
                    break;
                }
            }
            if (entry.model == 0 || skeleton.indices.find(entry.model) == skeleton.indices.end()) continue;

            const auto incoming = scene.incoming.find(node_id);
            if (incoming != scene.incoming.end()) {
                for (std::size_t connection_index : incoming->second) {
                    const Connection& connection = scene.connections[connection_index];
                    if (connection.type != "OP") continue;
                    const Object *curve_object = object(scene, connection.source);
                    if (!curve_object || curve_object->kind != "AnimationCurve") continue;
                    int axis = -1;
                    if (connection.property.find('X') != std::string::npos) axis = 0;
                    else if (connection.property.find('Y') != std::string::npos) axis = 1;
                    else if (connection.property.find('Z') != std::string::npos) axis = 2;
                    if (axis < 0) continue;
                    entry.axes[static_cast<std::size_t>(axis)] = parseCurve(*curve_object);
                    entry.present[static_cast<std::size_t>(axis)] = true;
                    const Curve& curve = entry.axes[static_cast<std::size_t>(axis)];
                    if (!curve.times.empty()) {
                        minimum_time = std::min(minimum_time, curve.times.front());
                        maximum_time = std::max(maximum_time, curve.times.back());
                    }
                }
            }
            animated_nodes.push_back(std::move(entry));
        }

        const std::vector<double> local_start = propertyValues(*stack, "LocalStart");
        const std::vector<double> local_stop = propertyValues(*stack, "LocalStop");
        if (!local_start.empty()) minimum_time = static_cast<std::int64_t>(local_start[0]);
        if (!local_stop.empty()) maximum_time = static_cast<std::int64_t>(local_stop[0]);
        if (minimum_time == std::numeric_limits<std::int64_t>::max()) minimum_time = 0;
        if (maximum_time < minimum_time) maximum_time = minimum_time;

        const double duration = static_cast<double>(maximum_time - minimum_time) / kFbxTicksPerSecond;
        std::size_t sample_count = duration > 0.0
            ? static_cast<std::size_t>(std::ceil(duration * kTargetSampleRate)) + 1u
            : 1u;
        sample_count = std::clamp<std::size_t>(sample_count, 1u, kMaximumClipSamples);

        Animation::AnimationClip clip;
        clip.name = stack->name.empty() ? "Animation" : stack->name;
        clip.duration = static_cast<float>(duration);
        clip.sample_rate = duration > 0.0 && sample_count > 1u
            ? static_cast<float>(static_cast<double>(sample_count - 1u) / duration)
            : kTargetSampleRate;
        clip.tracks.resize(skeleton.models.size());

        std::unordered_map<ObjectId, std::vector<const NodeCurves *>> by_model;
        for (const NodeCurves& entry : animated_nodes) by_model[entry.model].push_back(&entry);

        for (std::size_t bone = 0u; bone < skeleton.models.size(); ++bone) {
            const Object *model = object(scene, skeleton.models[bone]);
            if (!model) continue;
            const Trs bind = objectTrs(*model);
            Animation::Track& track = clip.tracks[bone];
            track.samples.reserve(sample_count);

            for (std::size_t sample = 0u; sample < sample_count; ++sample) {
                const double factor = sample_count > 1u
                    ? static_cast<double>(sample) / static_cast<double>(sample_count - 1u)
                    : 0.0;
                const std::int64_t time = minimum_time + static_cast<std::int64_t>(
                    static_cast<double>(maximum_time - minimum_time) * factor
                );
                Trs value = bind;
                const auto animated = by_model.find(skeleton.models[bone]);
                if (animated != by_model.end()) {
                    for (const NodeCurves *curves : animated->second) {
                        Animation::Vec3 *target = nullptr;
                        if (curves->property.find("Translation") != std::string::npos) target = &value.translation;
                        else if (curves->property.find("Rotation") != std::string::npos) target = &value.rotation_degrees;
                        else if (curves->property.find("Scaling") != std::string::npos) target = &value.scale;
                        if (!target) continue;
                        float *components[3] = {&target->x, &target->y, &target->z};
                        for (std::size_t axis = 0u; axis < 3u; ++axis) {
                            if (curves->present[axis]) {
                                *components[axis] = static_cast<float>(sampleCurve(
                                    curves->axes[axis], time, *components[axis]
                                ));
                            }
                        }
                    }
                }
                track.samples.push_back(animationTransform(value));
            }
        }
        result.push_back(std::move(clip));
    }
    return result;
}

} // namespace

bool load(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) {
        if (error) *error = "null FBX destination";
        return false;
    }
    *document = {};

    RawDocument raw;
    std::string parse_error;
    if (!FbxParser::parseFile(path, &raw, &parse_error)) {
        if (error) *error = "cannot parse FBX: " + path + ": " + parse_error;
        return false;
    }

    Scene scene;
    if (!buildScene(raw, &scene, error)) return false;
    const std::filesystem::path source_path(path);
    const SkeletonBuild skeleton = collectSkeleton(scene);
    document->has_skeleton = !skeleton.models.empty();
    if (document->has_skeleton) {
        document->skeleton = makeSkeleton(scene, skeleton, source_path.stem().string());
        document->animations = makeAnimations(scene, skeleton);
    }

    std::unordered_map<ObjectId, Matrix> global_cache;
    std::unordered_set<ObjectId> visiting;

    for (ObjectId geometry_id : scene.object_order) {
        const Object *geometry = object(scene, geometry_id);
        if (!geometry || geometry->kind != "Geometry" || geometry->subtype != "Mesh") continue;

        const std::vector<double> vertex_values = numericArray(child(geometry->node, "Vertices"));
        const std::size_t control_count = vertex_values.size() / 3u;
        if (control_count == 0u) continue;
        std::vector<Vec3> controls(control_count);
        for (std::size_t index = 0u; index < control_count; ++index) {
            controls[index] = {
                static_cast<float>(vertex_values[index * 3u + 0u]),
                static_cast<float>(vertex_values[index * 3u + 1u]),
                static_cast<float>(vertex_values[index * 3u + 2u]),
            };
        }

        const std::vector<Polygon> polygons = parsePolygons(*geometry->node, control_count);
        if (polygons.empty()) continue;
        const RawNode *normal_node = nullptr;
        const RawNode *uv_node = nullptr;
        const RawNode *material_node = nullptr;
        for (const RawNode& child_node : geometry->node->children) {
            if (!normal_node && child_node.name == "LayerElementNormal") normal_node = &child_node;
            if (!uv_node && child_node.name == "LayerElementUV") uv_node = &child_node;
            if (!material_node && child_node.name == "LayerElementMaterial") material_node = &child_node;
        }
        const LayerElement normals = parseLayerElement(normal_node, "Normals", "NormalsIndex", 3u);
        const LayerElement uvs = parseLayerElement(uv_node, "UV", "UVIndex", 2u);
        const LayerElement materials = parseLayerElement(material_node, "Materials", "Materials", 1u);

        const ObjectId model_id = geometryModel(scene, geometry_id);
        const std::vector<ObjectId> model_materials = modelMaterials(scene, model_id);
        const ObjectId skin_id = skinForGeometry(scene, geometry_id);
        const bool skinned = skin_id != 0 && document->has_skeleton;
        const std::vector<Animation::SkinWeights> weights = skinned
            ? buildControlWeights(scene, skin_id, skeleton, control_count)
            : std::vector<Animation::SkinWeights>(control_count);
        const std::vector<Matrix> bind_palette = skinned
            ? buildMeshBindPalette(scene, skin_id, skeleton)
            : std::vector<Matrix>{};

        Matrix model_matrix = identityMatrix();
        Matrix inverse_model = identityMatrix();
        if (!skinned && model_id != 0) {
            model_matrix = globalModelMatrix(scene, model_id, global_cache, visiting);
            invertMatrix(model_matrix, &inverse_model);
        }

        struct PartBuilder {
            MeshData mesh;
            ObjectId material = 0;
        };
        std::unordered_map<int, std::size_t> part_for_material;
        std::vector<PartBuilder> builders;

        for (const Polygon& polygon : polygons) {
            const int material_slot = polygonMaterial(materials, polygon.index);
            std::size_t builder_index = 0u;
            const auto existing = part_for_material.find(material_slot);
            if (existing == part_for_material.end()) {
                builder_index = builders.size();
                part_for_material.emplace(material_slot, builder_index);
                PartBuilder builder;
                if (
                    material_slot >= 0 &&
                    static_cast<std::size_t>(material_slot) < model_materials.size())
                {
                    builder.material = model_materials[static_cast<std::size_t>(material_slot)];
                }
                builder.mesh.skin_inverse_bind = bind_palette;
                builders.push_back(std::move(builder));
            } else {
                builder_index = existing->second;
            }
            PartBuilder& builder = builders[builder_index];

            for (std::size_t triangle_corner = 1u; triangle_corner + 1u < polygon.corners.size(); ++triangle_corner) {
                const PolygonCorner corners[3] = {
                    polygon.corners[0],
                    polygon.corners[triangle_corner],
                    polygon.corners[triangle_corner + 1u],
                };
                Vec3 face_normal = normalize(cross(
                    subtract(controls[corners[1].control], controls[corners[0].control]),
                    subtract(controls[corners[2].control], controls[corners[0].control])
                ));

                for (const PolygonCorner& corner : corners) {
                    Vertex vertex;
                    vertex.position = controls[corner.control];
                    vertex.normal = layerVec3(
                        normals,
                        polygon.index,
                        corner.polygon_vertex,
                        corner.control,
                        face_normal
                    );
                    vertex.uv = layerVec2(uvs, polygon.index, corner.polygon_vertex, corner.control);
                    if (corner.control < weights.size()) vertex.skin = weights[corner.control];

                    if (!skinned) {
                        vertex.position = transformPoint(model_matrix, vertex.position);
                        vertex.normal = transformNormal(inverse_model, vertex.normal);
                    }

                    const std::uint32_t index = static_cast<std::uint32_t>(builder.mesh.vertices.size());
                    builder.mesh.vertices.push_back(vertex);
                    builder.mesh.indices.push_back(index);
                }
            }
        }

        for (PartBuilder& builder : builders) {
            if (builder.mesh.indices.empty()) continue;
            builder.mesh.bounds = calculateBounds(builder.mesh.vertices);
            Part part;
            part.mesh = std::move(builder.mesh);
            part.material = builder.material != 0
                ? convertMaterial(scene, builder.material, source_path)
                : MaterialData{};
            if (part.material.name.empty()) part.material.name = geometry->name;
            document->parts.push_back(std::move(part));
        }
    }

    if (document->parts.empty()) {
        if (error) *error = "FBX contains no renderable mesh geometry: " + path;
        return false;
    }
    return true;
}

} // namespace Models::Fbx
