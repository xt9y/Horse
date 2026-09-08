#include "Models/Formats/FbxScene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Models::FbxInternal {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

const FbxDocument::Node *globalPropertyNode(
    const FbxDocument::Document& document,
    std::string_view name)
{
    const FbxDocument::Node *settings = document.root.child("GlobalSettings");
    const FbxDocument::Node *properties = settings ? settings->child("Properties70") : nullptr;
    if (!properties) return nullptr;
    for (const auto& property : properties->children) {
        if (property.name != "P" || property.properties.empty()) continue;
        if (property.properties[0].asString() == name) return &property;
    }
    return nullptr;
}

double globalScalar(
    const FbxDocument::Document& document,
    std::string_view name,
    double fallback)
{
    const auto *property = globalPropertyNode(document, name);
    if (!property || property->properties.size() <= 4u) return fallback;
    return property->properties[4].asDouble(fallback);
}

std::int64_t globalInteger(
    const FbxDocument::Document& document,
    std::string_view name,
    std::int64_t fallback)
{
    const auto *property = globalPropertyNode(document, name);
    if (!property || property->properties.size() <= 4u) return fallback;
    return property->properties[4].asInt64(fallback);
}

Animation::Vec3 axisVector(int axis, int sign)
{
    Animation::Vec3 result{};
    const float value = static_cast<float>(sign);
    if (axis == 0) result.x = value;
    else if (axis == 1) result.y = value;
    else result.z = value;
    return result;
}

float determinant3(const Animation::Mat4& matrix)
{
    const float a00 = matrix.value[0];
    const float a01 = matrix.value[4];
    const float a02 = matrix.value[8];
    const float a10 = matrix.value[1];
    const float a11 = matrix.value[5];
    const float a12 = matrix.value[9];
    const float a20 = matrix.value[2];
    const float a21 = matrix.value[6];
    const float a22 = matrix.value[10];
    return
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
}

Animation::Mat4 transposeOrientation(const Animation::Mat4& matrix)
{
    Animation::Mat4 result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.value[column * 4 + row] = matrix.value[row * 4 + column];
        }
    }
    return result;
}

Animation::Vec3 orient(const Animation::Mat4& matrix, Animation::Vec3 value)
{
    return {
        matrix.value[0] * value.x + matrix.value[4] * value.y + matrix.value[8] * value.z,
        matrix.value[1] * value.x + matrix.value[5] * value.y + matrix.value[9] * value.z,
        matrix.value[2] * value.x + matrix.value[6] * value.y + matrix.value[10] * value.z,
    };
}

bool parseBasis(const FbxDocument::Document& document, CanonicalBasis *basis, std::string *error)
{
    if (!basis) return fail(error, "null FBX canonical basis destination");

    const double unit = globalScalar(document, "UnitScaleFactor", 1.0);
    const int up_axis = static_cast<int>(globalInteger(document, "UpAxis", 1));
    const int up_sign = static_cast<int>(globalInteger(document, "UpAxisSign", 1));
    const int front_axis = static_cast<int>(globalInteger(document, "FrontAxis", 2));
    const int front_sign = static_cast<int>(globalInteger(document, "FrontAxisSign", -1));
    const int coord_axis = static_cast<int>(globalInteger(document, "CoordAxis", 0));
    const int coord_sign = static_cast<int>(globalInteger(document, "CoordAxisSign", 1));

    if (!std::isfinite(unit) || unit <= 0.0) {
        return fail(error, "FBX GlobalSettings has invalid UnitScaleFactor");
    }
    const std::array<int, 3> axes{coord_axis, up_axis, front_axis};
    for (int axis : axes) {
        if (axis < 0 || axis > 2) {
            return fail(error, "FBX GlobalSettings contains an axis outside [0,2]");
        }
    }
    if (coord_axis == up_axis || coord_axis == front_axis || up_axis == front_axis) {
        return fail(error, "FBX GlobalSettings contains duplicate coordinate axes");
    }
    if ((coord_sign != -1 && coord_sign != 1) ||
        (up_sign != -1 && up_sign != 1) ||
        (front_sign != -1 && front_sign != 1)) {
        return fail(error, "FBX GlobalSettings axis signs must be -1 or +1");
    }

    const Animation::Vec3 right = axisVector(coord_axis, coord_sign);
    const Animation::Vec3 up = axisVector(up_axis, up_sign);
    const Animation::Vec3 front = axisVector(front_axis, front_sign);

    // Horse canonical space is +X right, +Y up, and -Z forward. Rows of
    // this orthonormal matrix project FBX coordinates onto those axes.
    Animation::Mat4 orientation;
    orientation.value[0] = right.x;
    orientation.value[4] = right.y;
    orientation.value[8] = right.z;
    orientation.value[1] = up.x;
    orientation.value[5] = up.y;
    orientation.value[9] = up.z;
    orientation.value[2] = -front.x;
    orientation.value[6] = -front.y;
    orientation.value[10] = -front.z;

    const float determinant = determinant3(orientation);
    if (!std::isfinite(determinant) || std::abs(std::abs(determinant) - 1.0f) > 1.0e-4f) {
        return fail(error, "FBX GlobalSettings does not define an orthonormal axis basis");
    }

    basis->fbx_to_horse = orientation;
    basis->horse_to_fbx = transposeOrientation(orientation);
    basis->unit_scale = static_cast<float>(unit / 100.0);
    basis->mirrored = determinant < 0.0f;
    return true;
}

} // namespace

std::string cleanName(std::string value)
{
    const std::size_t nul = value.find('\0');
    if (nul != std::string::npos) value.resize(nul);
    const std::size_t separator = value.rfind("::");
    if (separator != std::string::npos) value = value.substr(separator + 2u);
    return value;
}

const FbxDocument::Node *child(const FbxDocument::Node *node, std::string_view name)
{
    if (!node) return nullptr;
    for (const auto& value : node->children) {
        if (value.name == name) return &value;
    }
    return nullptr;
}

const FbxDocument::Node *propertyNode(const Object& object_value, std::string_view name)
{
    const auto *properties = child(object_value.node, "Properties70");
    if (!properties) return nullptr;
    for (const auto& property : properties->children) {
        if (property.name != "P" || property.properties.empty()) continue;
        if (property.properties[0].asString() == name) return &property;
    }
    return nullptr;
}

std::vector<double> propertyValues(const Object& object_value, std::string_view name)
{
    const auto *property = propertyNode(object_value, name);
    if (!property || property->properties.size() <= 4u) return {};
    std::vector<double> result;
    result.reserve(property->properties.size() - 4u);
    for (std::size_t index = 4u; index < property->properties.size(); ++index) {
        result.push_back(property->properties[index].asDouble());
    }
    return result;
}

double propertyScalar(const Object& object_value, std::string_view name, double fallback)
{
    const auto values = propertyValues(object_value, name);
    return values.empty() ? fallback : values.front();
}

std::int64_t propertyInteger(const Object& object_value, std::string_view name, std::int64_t fallback)
{
    const auto *property = propertyNode(object_value, name);
    if (!property || property->properties.size() <= 4u) return fallback;
    return property->properties[4].asInt64(fallback);
}

Animation::Vec3 propertyVec3(
    const Object& object_value,
    std::string_view name,
    Animation::Vec3 fallback)
{
    const auto values = propertyValues(object_value, name);
    if (values.size() < 3u) return fallback;
    return {
        static_cast<float>(values[0]),
        static_cast<float>(values[1]),
        static_cast<float>(values[2]),
    };
}

bool buildScene(const FbxDocument::Document& document, Scene *scene, std::string *error)
{
    if (error) error->clear();
    if (!scene) return fail(error, "null FBX scene destination");
    *scene = {};
    scene->document = &document;

    if (!parseBasis(document, &scene->basis, error)) return false;

    const auto *objects = document.root.child("Objects");
    if (!objects) return fail(error, "FBX has no Objects section");

    for (const auto& node : objects->children) {
        if (node.properties.empty()) continue;
        Object value;
        value.id = node.properties[0].asInt64();
        value.kind = node.name;
        value.name = node.properties.size() > 1u ? cleanName(node.properties[1].asString()) : std::string{};
        value.subtype = node.properties.size() > 2u ? node.properties[2].asString() : std::string{};
        value.node = &node;
        if (scene->objects.find(value.id) != scene->objects.end()) {
            return fail(error, "FBX contains duplicate object id " + std::to_string(value.id));
        }
        scene->object_order.push_back(value.id);
        scene->objects.emplace(value.id, std::move(value));
    }

    if (const auto *connections = document.root.child("Connections")) {
        for (const auto& raw : connections->children) {
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
    for (std::size_t index : found->second) {
        const Connection& connection = scene.connections[index];
        if (connection.type != "OO") continue;
        const Object *candidate = object(scene, connection.destination);
        if (candidate && candidate->kind == "Model") return candidate->id;
    }
    return 0;
}

ObjectId geometryModel(const Scene& scene, ObjectId geometry)
{
    const auto found = scene.outgoing.find(geometry);
    if (found == scene.outgoing.end()) return 0;
    for (std::size_t index : found->second) {
        const Connection& connection = scene.connections[index];
        if (connection.type != "OO") continue;
        const Object *candidate = object(scene, connection.destination);
        if (candidate && candidate->kind == "Model") return candidate->id;
    }
    return 0;
}

std::vector<ObjectId> connectedObjects(const Scene& scene, ObjectId id, std::string_view kind)
{
    std::vector<ObjectId> result;
    std::unordered_set<ObjectId> seen;
    auto append = [&](const std::vector<std::size_t>& indexes, bool incoming) {
        for (std::size_t index : indexes) {
            const Connection& connection = scene.connections[index];
            if (connection.type != "OO" && connection.type != "OP") continue;
            const ObjectId candidate_id = incoming ? connection.source : connection.destination;
            const Object *candidate = object(scene, candidate_id);
            if (!candidate || candidate->kind != kind || !seen.insert(candidate_id).second) continue;
            result.push_back(candidate_id);
        }
    };
    if (const auto found = scene.incoming.find(id); found != scene.incoming.end()) append(found->second, true);
    if (const auto found = scene.outgoing.find(id); found != scene.outgoing.end()) append(found->second, false);
    return result;
}

std::vector<ObjectId> modelMaterials(const Scene& scene, ObjectId model)
{
    return connectedObjects(scene, model, "Material");
}

Animation::Vec3 canonicalPoint(const CanonicalBasis& basis, Animation::Vec3 value)
{
    Animation::Vec3 result = orient(basis.fbx_to_horse, value);
    result.x *= basis.unit_scale;
    result.y *= basis.unit_scale;
    result.z *= basis.unit_scale;
    return result;
}

Animation::Vec3 canonicalVector(const CanonicalBasis& basis, Animation::Vec3 value)
{
    return orient(basis.fbx_to_horse, value);
}

Animation::Mat4 canonicalMatrix(const CanonicalBasis& basis, const Animation::Mat4& matrix)
{
    Animation::Mat4 result = Animation::multiply(
        Animation::multiply(basis.fbx_to_horse, matrix),
        basis.horse_to_fbx
    );
    result.value[12] *= basis.unit_scale;
    result.value[13] *= basis.unit_scale;
    result.value[14] *= basis.unit_scale;
    return result;
}

} // namespace Models::FbxInternal
