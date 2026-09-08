#ifndef RW_ENGINE_MODELS_FORMATS_FBX_SCENE_HPP
#define RW_ENGINE_MODELS_FORMATS_FBX_SCENE_HPP

#include "Animation/Animation.hpp"
#include "Models/Formats/FbxDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Models::FbxInternal {

using ObjectId = std::int64_t;

struct Object {
    ObjectId id = 0;
    std::string kind;
    std::string name;
    std::string subtype;
    const FbxDocument::Node *node = nullptr;
};

struct Connection {
    std::string type;
    ObjectId source = 0;
    ObjectId destination = 0;
    std::string property;
};

struct CanonicalBasis {
    Animation::Mat4 fbx_to_horse{};
    Animation::Mat4 horse_to_fbx{};
    float unit_scale = 0.01f;
    bool mirrored = false;
};

struct Scene {
    const FbxDocument::Document *document = nullptr;
    std::unordered_map<ObjectId, Object> objects;
    std::vector<ObjectId> object_order;
    std::vector<Connection> connections;
    std::unordered_map<ObjectId, std::vector<std::size_t>> incoming;
    std::unordered_map<ObjectId, std::vector<std::size_t>> outgoing;
    CanonicalBasis basis;
};

bool buildScene(const FbxDocument::Document& document, Scene *scene, std::string *error);
const Object *object(const Scene& scene, ObjectId id);
ObjectId parentModel(const Scene& scene, ObjectId model);
ObjectId geometryModel(const Scene& scene, ObjectId geometry);
std::vector<ObjectId> connectedObjects(const Scene& scene, ObjectId id, std::string_view kind);
std::vector<ObjectId> modelMaterials(const Scene& scene, ObjectId model);

const FbxDocument::Node *child(const FbxDocument::Node *node, std::string_view name);
const FbxDocument::Node *propertyNode(const Object& object, std::string_view name);
std::vector<double> propertyValues(const Object& object, std::string_view name);
double propertyScalar(const Object& object, std::string_view name, double fallback);
std::int64_t propertyInteger(const Object& object, std::string_view name, std::int64_t fallback);
Animation::Vec3 propertyVec3(const Object& object, std::string_view name, Animation::Vec3 fallback);
std::string cleanName(std::string value);

Animation::Vec3 canonicalPoint(const CanonicalBasis& basis, Animation::Vec3 value);
Animation::Vec3 canonicalVector(const CanonicalBasis& basis, Animation::Vec3 value);
Animation::Mat4 canonicalMatrix(const CanonicalBasis& basis, const Animation::Mat4& matrix);

} // namespace Models::FbxInternal

#endif
