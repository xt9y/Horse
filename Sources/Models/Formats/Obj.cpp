#include "Models/Formats/Obj.hpp"

#include "Models/Core/Material.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace Models::Obj {
namespace {

struct Index {
    int position = -1;
    int uv = -1;
    int normal = -1;
};

Vec3 subtract(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(const Vec3& value)
{
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 1.0e-8f) return {0.0f, 0.0f, 1.0f};
    return {value.x / length, value.y / length, value.z / length};
}

int resolveIndex(int index, std::size_t count)
{
    if (index > 0) {
        const int resolved = index - 1;
        return resolved >= 0 && resolved < static_cast<int>(count) ? resolved : -1;
    }
    if (index < 0) {
        const int resolved = static_cast<int>(count) + index;
        return resolved >= 0 && resolved < static_cast<int>(count) ? resolved : -1;
    }
    return -1;
}

bool parseIndex(const std::string& text, std::size_t positions, std::size_t uvs, std::size_t normals, Index *out)
{
    if (!out) return false;

    std::array<std::string, 3> fields;
    std::size_t field = 0;
    for (const char c : text) {
        if (c == '/' && field < 2) ++field;
        else fields[field].push_back(c);
    }

    char *end = nullptr;
    const long p = std::strtol(fields[0].c_str(), &end, 10);
    if (fields[0].empty() || end == fields[0].c_str() || *end != '\0') return false;
    out->position = resolveIndex(static_cast<int>(p), positions);
    if (out->position < 0) return false;

    if (!fields[1].empty()) {
        const long t = std::strtol(fields[1].c_str(), &end, 10);
        if (end == fields[1].c_str() || *end != '\0') return false;
        out->uv = resolveIndex(static_cast<int>(t), uvs);
        if (out->uv < 0) return false;
    }

    if (!fields[2].empty()) {
        const long n = std::strtol(fields[2].c_str(), &end, 10);
        if (end == fields[2].c_str() || *end != '\0') return false;
        out->normal = resolveIndex(static_cast<int>(n), normals);
        if (out->normal < 0) return false;
    }

    return true;
}

Bounds calculateBounds(const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return {};

    const float max = std::numeric_limits<float>::max();
    Bounds bounds {{max, max, max}, {-max, -max, -max}};
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

struct Builder {
    std::string material_name;
    MeshData mesh;
};

} // namespace

bool load(const std::string& path, Document *document, std::string *error)
{
    if (error) error->clear();
    if (!document) {
        if (error) *error = "null OBJ destination";
        return false;
    }
    *document = {};

    std::ifstream input(path);
    if (!input) {
        if (error) *error = "cannot open OBJ: " + path;
        return false;
    }

    const std::filesystem::path object_path(path);
    MaterialMap materials;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Builder> builders(1);
    Builder *builder = &builders.back();

    auto selectBuilder = [&](const std::string& material_name) {
        if (builder->mesh.indices.empty() && builder->mesh.vertices.empty()) {
            builder->material_name = material_name;
            return;
        }
        builders.push_back({});
        builder = &builders.back();
        builder->material_name = material_name;
    };

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::istringstream stream(line);
        std::string key;
        stream >> key;
        if (key.empty() || key[0] == '#') continue;

        if (key == "v") {
            Vec3 value;
            if (!(stream >> value.x >> value.y >> value.z)) {
                if (error) *error = "invalid OBJ vertex at line " + std::to_string(line_number);
                return false;
            }
            positions.push_back(value);
        } else if (key == "vn") {
            Vec3 value;
            if (!(stream >> value.x >> value.y >> value.z)) {
                if (error) *error = "invalid OBJ normal at line " + std::to_string(line_number);
                return false;
            }
            normals.push_back(normalize(value));
        } else if (key == "vt") {
            Vec2 value;
            if (!(stream >> value.x >> value.y)) {
                if (error) *error = "invalid OBJ texcoord at line " + std::to_string(line_number);
                return false;
            }
            uvs.push_back(value);
        } else if (key == "mtllib") {
            std::string library;
            std::getline(stream >> std::ws, library);
            MaterialMap loaded;
            std::string material_error;
            const std::filesystem::path material_path =
                (object_path.parent_path() / library).lexically_normal();
            if (!loadMaterialLibrary(material_path.string(), &loaded, &material_error)) {
                if (error) {
                    *error = "failed to load OBJ material library '" + material_path.string() + "'";
                    if (!material_error.empty()) *error += ": " + material_error;
                }
                return false;
            }
            materials.insert(loaded.begin(), loaded.end());
        } else if (key == "usemtl") {
            std::string name;
            std::getline(stream >> std::ws, name);
            selectBuilder(name);
        } else if (key == "f") {
            std::vector<Index> polygon;
            std::string token;
            while (stream >> token) {
                Index index;
                if (!parseIndex(token, positions.size(), uvs.size(), normals.size(), &index)) {
                    if (error) *error = "invalid OBJ face at line " + std::to_string(line_number);
                    return false;
                }
                polygon.push_back(index);
            }
            if (polygon.size() < 3u) {
                if (error) *error = "OBJ face has fewer than three vertices at line " + std::to_string(line_number);
                return false;
            }

            for (std::size_t triangle = 1; triangle + 1 < polygon.size(); ++triangle) {
                const std::array<Index, 3> corners {polygon[0], polygon[triangle], polygon[triangle + 1]};
                const Vec3 face_normal = normalize(cross(
                    subtract(positions[corners[1].position], positions[corners[0].position]),
                    subtract(positions[corners[2].position], positions[corners[0].position])
                ));

                for (const Index& index : corners) {
                    Vertex vertex;
                    vertex.position = positions[index.position];
                    vertex.normal = index.normal >= 0 ? normals[index.normal] : face_normal;
                    vertex.uv = index.uv >= 0 ? uvs[index.uv] : Vec2{};
                    builder->mesh.indices.push_back(static_cast<std::uint32_t>(builder->mesh.vertices.size()));
                    builder->mesh.vertices.push_back(vertex);
                }
            }
        }
    }

    for (Builder& value : builders) {
        if (value.mesh.indices.empty()) continue;
        value.mesh.bounds = calculateBounds(value.mesh.vertices);
        MaterialData material;
        const auto found = materials.find(value.material_name);
        if (found != materials.end()) material = found->second;
        document->parts.push_back({std::move(value.mesh), std::move(material)});
    }

    if (document->parts.empty()) {
        if (error) *error = "OBJ contains no renderable faces: " + path;
        return false;
    }
    return true;
}

} // namespace Models::Obj
