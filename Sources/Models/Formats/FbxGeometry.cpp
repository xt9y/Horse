#include "Models/Formats/FbxGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace Models::FbxInternal {
namespace {

struct PolygonCorner {
    std::uint32_t control = 0u;
    std::size_t polygon_vertex = 0u;
};

struct Polygon {
    std::vector<PolygonCorner> corners;
    std::size_t index = 0u;
};

struct LayerElement {
    bool present = false;
    std::string mapping = "ByPolygonVertex";
    std::string reference = "Direct";
    std::vector<double> direct;
    std::vector<std::int32_t> indices;
    std::size_t tuple_size = 0u;
};

bool fail(std::string *error, const std::string& message)
{
    if (error && error->empty()) *error = message;
    return false;
}

std::vector<double> numericArray(const FbxDocument::Node *node)
{
    return node ? node->numericArray() : std::vector<double>{};
}

std::vector<std::int32_t> int32Array(const FbxDocument::Node *node)
{
    if (!node || node->properties.empty()) return {};
    if (const auto *values = node->properties[0].asInt32Array()) return *values;
    if (const auto *values64 = node->properties[0].asInt64Array()) {
        std::vector<std::int32_t> out;
        out.reserve(values64->size());
        for (std::int64_t value : *values64) {
            if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) return {};
            out.push_back(static_cast<std::int32_t>(value));
        }
        return out;
    }
    const auto values = node->numericArray();
    std::vector<std::int32_t> out;
    out.reserve(values.size());
    for (double value : values) out.push_back(static_cast<std::int32_t>(value));
    return out;
}

Models::Vec3 normalize(Models::Vec3 value)
{
    const float length_sq = value.x*value.x + value.y*value.y + value.z*value.z;
    if (length_sq <= 1.0e-20f) return {0.0f, 0.0f, 1.0f};
    const float inv = 1.0f / std::sqrt(length_sq);
    return {value.x*inv, value.y*inv, value.z*inv};
}

Models::Vec3 sub(Models::Vec3 a, Models::Vec3 b)
{
    return {a.x-b.x, a.y-b.y, a.z-b.z};
}

Models::Vec3 cross(Models::Vec3 a, Models::Vec3 b)
{
    return {
        a.y*b.z-a.z*b.y,
        a.z*b.x-a.x*b.z,
        a.x*b.y-a.y*b.x,
    };
}

Models::Vec3 transformPoint(const Animation::Mat4& matrix, Models::Vec3 value)
{
    const auto result = Animation::transformPoint(matrix, {value.x, value.y, value.z});
    return {result.x, result.y, result.z};
}

Models::Vec3 transformNormal(const Animation::Mat4& matrix, Models::Vec3 value)
{
    Animation::Mat4 inverse;
    if (!invertMatrix(matrix, &inverse)) return normalize(value);
    Models::Vec3 result{
        inverse.value[0]*value.x + inverse.value[1]*value.y + inverse.value[2]*value.z,
        inverse.value[4]*value.x + inverse.value[5]*value.y + inverse.value[6]*value.z,
        inverse.value[8]*value.x + inverse.value[9]*value.y + inverse.value[10]*value.z,
    };
    return normalize(result);
}

Bounds calculateBounds(const std::vector<Vertex>& vertices)
{
    if (vertices.empty()) return {};
    const float maximum = std::numeric_limits<float>::max();
    Bounds bounds{{maximum,maximum,maximum},{-maximum,-maximum,-maximum}};
    for (const auto& vertex : vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    return bounds;
}

bool parsePolygons(
    const FbxDocument::Node& geometry,
    std::size_t control_count,
    std::vector<Polygon> *out,
    std::string *error,
    const std::string& geometry_name)
{
    if (!out) return fail(error, "null FBX polygon destination");
    const auto raw_indices = int32Array(child(&geometry, "PolygonVertexIndex"));
    if (raw_indices.empty()) return fail(error, "Geometry `" + geometry_name + "` has no PolygonVertexIndex data");
    Polygon current;
    std::size_t polygon_vertex = 0u;
    for (std::int32_t raw : raw_indices) {
        const bool last = raw < 0;
        const std::int64_t decoded = last ? -static_cast<std::int64_t>(raw)-1ll : raw;
        if (decoded < 0 || static_cast<std::size_t>(decoded) >= control_count) {
            return fail(error, "Geometry `" + geometry_name + "` references an out-of-range control point");
        }
        current.corners.push_back({static_cast<std::uint32_t>(decoded), polygon_vertex++});
        if (last) {
            if (current.corners.size() < 3u) return fail(error, "Geometry `" + geometry_name + "` contains a polygon with fewer than three corners");
            current.index = out->size();
            out->push_back(std::move(current));
            current = {};
        }
    }
    if (!current.corners.empty()) return fail(error, "Geometry `" + geometry_name + "` has an unterminated polygon");
    return !out->empty();
}

LayerElement parseLayer(
    const FbxDocument::Node *node,
    const char *direct_name,
    const char *index_name,
    std::size_t tuple_size)
{
    LayerElement result;
    result.present = node != nullptr;
    result.tuple_size = tuple_size;
    if (!node) return result;
    if (const auto *mapping = child(node, "MappingInformationType"); mapping && !mapping->properties.empty()) {
        result.mapping = mapping->properties[0].asString(result.mapping);
    }
    if (const auto *reference = child(node, "ReferenceInformationType"); reference && !reference->properties.empty()) {
        result.reference = reference->properties[0].asString(result.reference);
    }
    result.direct = numericArray(child(node, direct_name));
    if (index_name) result.indices = int32Array(child(node, index_name));
    return result;
}

bool validateLayerModes(const LayerElement& layer, const std::string& label, std::string *error)
{
    if (!layer.present) return true;
    const bool mapping_ok = layer.mapping == "ByPolygonVertex" || layer.mapping == "ByVertex" ||
        layer.mapping == "ByVertice" || layer.mapping == "ByPolygon" || layer.mapping == "AllSame";
    if (!mapping_ok) return fail(error, "unsupported FBX " + label + " mapping mode `" + layer.mapping + "`");
    const bool reference_ok = layer.reference == "Direct" || layer.reference == "IndexToDirect" || layer.reference == "Index";
    if (!reference_ok) return fail(error, "unsupported FBX " + label + " reference mode `" + layer.reference + "`");
    return true;
}

bool mappedIndex(
    const LayerElement& layer,
    std::size_t polygon,
    std::size_t polygon_vertex,
    std::uint32_t control,
    std::size_t *out,
    std::string *error,
    const std::string& label)
{
    if (!out) return false;
    std::size_t mapped = polygon_vertex;
    if (layer.mapping == "ByVertex" || layer.mapping == "ByVertice") mapped = control;
    else if (layer.mapping == "ByPolygon") mapped = polygon;
    else if (layer.mapping == "AllSame") mapped = 0u;

    if (layer.reference == "IndexToDirect" || layer.reference == "Index") {
        if (mapped >= layer.indices.size() || layer.indices[mapped] < 0) {
            return fail(error, label + " IndexToDirect reference is out of range");
        }
        mapped = static_cast<std::size_t>(layer.indices[mapped]);
    }
    *out = mapped;
    return true;
}

bool layerNormal(
    const LayerElement& layer,
    std::size_t polygon,
    const PolygonCorner& corner,
    Models::Vec3 *out,
    std::string *error)
{
    if (!layer.present) return false;
    std::size_t index = 0u;
    if (!mappedIndex(layer, polygon, corner.polygon_vertex, corner.control, &index, error, "normal")) return false;
    const std::size_t offset = index * 3u;
    if (offset + 2u >= layer.direct.size()) {
        fail(error, "normal layer direct data is out of range");
        return false;
    }
    *out = normalize({
        static_cast<float>(layer.direct[offset]),
        static_cast<float>(layer.direct[offset+1u]),
        static_cast<float>(layer.direct[offset+2u]),
    });
    return true;
}

bool layerUv(
    const LayerElement& layer,
    std::size_t polygon,
    const PolygonCorner& corner,
    Models::Vec2 *out,
    std::string *error)
{
    if (!layer.present) {
        *out = {};
        return true;
    }
    std::size_t index = 0u;
    if (!mappedIndex(layer, polygon, corner.polygon_vertex, corner.control, &index, error, "UV")) return false;
    const std::size_t offset = index * 2u;
    if (offset + 1u >= layer.direct.size()) return fail(error, "UV layer direct data is out of range");
    *out = {static_cast<float>(layer.direct[offset]), static_cast<float>(layer.direct[offset+1u])};
    return true;
}

bool materialSlot(
    const LayerElement& layer,
    std::size_t polygon,
    int *out,
    std::string *error)
{
    if (!out) return false;
    if (!layer.present || (layer.direct.empty() && layer.indices.empty())) {
        *out = 0;
        return true;
    }
    std::size_t mapped = layer.mapping == "AllSame" ? 0u : polygon;
    const std::vector<std::int32_t>& values = layer.indices;
    if (!values.empty()) {
        if (mapped >= values.size() || values[mapped] < 0) return fail(error, "material layer index is out of range");
        *out = values[mapped];
        return true;
    }
    if (mapped >= layer.direct.size() || layer.direct[mapped] < 0.0) return fail(error, "material layer direct index is out of range");
    *out = static_cast<int>(layer.direct[mapped]);
    return true;
}

struct P2 { float x; float y; };

float area2(P2 a, P2 b, P2 c)
{
    return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}

bool pointInTriangle(P2 p, P2 a, P2 b, P2 c, float sign)
{
    const float e0 = area2(a,b,p) * sign;
    const float e1 = area2(b,c,p) * sign;
    const float e2 = area2(c,a,p) * sign;
    return e0 >= -1.0e-7f && e1 >= -1.0e-7f && e2 >= -1.0e-7f;
}

bool triangulate(
    const Polygon& polygon,
    const std::vector<Models::Vec3>& controls,
    std::vector<std::array<std::size_t,3>> *out,
    std::string *error)
{
    if (polygon.corners.size() == 3u) {
        out->push_back({0u,1u,2u});
        return true;
    }

    Models::Vec3 normal{};
    for (std::size_t i=0;i<polygon.corners.size();++i) {
        const auto& a = controls[polygon.corners[i].control];
        const auto& b = controls[polygon.corners[(i+1u)%polygon.corners.size()].control];
        normal.x += (a.y-b.y)*(a.z+b.z);
        normal.y += (a.z-b.z)*(a.x+b.x);
        normal.z += (a.x-b.x)*(a.y+b.y);
    }
    const float ax=std::abs(normal.x), ay=std::abs(normal.y), az=std::abs(normal.z);
    int drop = ax > ay ? (ax > az ? 0 : 2) : (ay > az ? 1 : 2);
    std::vector<P2> projected;
    projected.reserve(polygon.corners.size());
    for (const auto& corner : polygon.corners) {
        const auto& p = controls[corner.control];
        if (drop==0) projected.push_back({p.y,p.z});
        else if (drop==1) projected.push_back({p.x,p.z});
        else projected.push_back({p.x,p.y});
    }
    float signed_area=0.0f;
    for (std::size_t i=0;i<projected.size();++i) {
        const auto& a=projected[i]; const auto& b=projected[(i+1u)%projected.size()];
        signed_area += a.x*b.y-b.x*a.y;
    }
    if (std::abs(signed_area) <= 1.0e-8f) return fail(error, "FBX polygon is degenerate and cannot be triangulated");
    const float sign = signed_area > 0.0f ? 1.0f : -1.0f;

    std::vector<std::size_t> remaining(projected.size());
    for (std::size_t i=0;i<remaining.size();++i) remaining[i]=i;
    std::size_t guard=0u;
    while (remaining.size()>3u && guard++ < polygon.corners.size()*polygon.corners.size()) {
        bool clipped=false;
        for (std::size_t i=0;i<remaining.size();++i) {
            const std::size_t prev=remaining[(i+remaining.size()-1u)%remaining.size()];
            const std::size_t cur=remaining[i];
            const std::size_t next=remaining[(i+1u)%remaining.size()];
            if (area2(projected[prev],projected[cur],projected[next])*sign <= 1.0e-8f) continue;
            bool contains=false;
            for (std::size_t candidate : remaining) {
                if (candidate==prev || candidate==cur || candidate==next) continue;
                if (pointInTriangle(projected[candidate],projected[prev],projected[cur],projected[next],sign)) {
                    contains=true; break;
                }
            }
            if (contains) continue;
            out->push_back({prev,cur,next});
            remaining.erase(remaining.begin()+static_cast<std::ptrdiff_t>(i));
            clipped=true;
            break;
        }
        if (!clipped) return fail(error, "FBX polygon is self-intersecting or otherwise not triangulatable");
    }
    if (remaining.size()!=3u) return fail(error, "FBX polygon triangulation did not converge");
    out->push_back({remaining[0],remaining[1],remaining[2]});
    return true;
}

} // namespace

bool convertGeometry(
    const Scene& scene,
    ObjectId geometry_id,
    const std::vector<ObjectId>& model_materials,
    const std::vector<Animation::SkinWeights>& control_weights,
    const std::vector<Animation::Mat4>& bind_palette,
    bool skinned,
    std::vector<GeometryPart> *out,
    std::string *error)
{
    if (!out) return fail(error, "null FBX geometry destination");
    out->clear();
    const Object *geometry = object(scene, geometry_id);
    if (!geometry || geometry->kind != "Geometry" || geometry->subtype != "Mesh") {
        return fail(error, "invalid FBX mesh Geometry id " + std::to_string(geometry_id));
    }

    const auto vertex_values = numericArray(child(geometry->node, "Vertices"));
    if (vertex_values.empty() || vertex_values.size()%3u != 0u) {
        return fail(error, "Geometry `"+geometry->name+"` has malformed Vertices data");
    }
    const std::size_t control_count=vertex_values.size()/3u;
    std::vector<Models::Vec3> controls(control_count);
    for (std::size_t i=0;i<control_count;++i) {
        const Animation::Vec3 source{
            static_cast<float>(vertex_values[i*3u]),
            static_cast<float>(vertex_values[i*3u+1u]),
            static_cast<float>(vertex_values[i*3u+2u]),
        };
        const auto canonical=canonicalPoint(scene.basis,source);
        controls[i]={canonical.x,canonical.y,canonical.z};
    }

    std::vector<Polygon> polygons;
    if (!parsePolygons(*geometry->node, control_count, &polygons, error, geometry->name)) return false;

    const FbxDocument::Node *normal_node=nullptr,*uv_node=nullptr,*material_node=nullptr;
    for (const auto& node : geometry->node->children) {
        if (!normal_node && node.name=="LayerElementNormal") normal_node=&node;
        if (!uv_node && node.name=="LayerElementUV") uv_node=&node;
        if (!material_node && node.name=="LayerElementMaterial") material_node=&node;
    }
    LayerElement normals=parseLayer(normal_node,"Normals","NormalsIndex",3u);
    LayerElement uvs=parseLayer(uv_node,"UV","UVIndex",2u);
    LayerElement materials=parseLayer(material_node,"Materials","Materials",1u);
    if (!validateLayerModes(normals,"normal",error) || !validateLayerModes(uvs,"UV",error) || !validateLayerModes(materials,"material",error)) return false;

    const ObjectId model_id=geometryModel(scene,geometry_id);
    Animation::Mat4 final_matrix;
    if (model_id!=0) {
        TransformProperties properties;
        const Object *model=object(scene,model_id);
        if (!model || !readTransformProperties(*model,&properties,error)) return false;
        Animation::Mat4 geometric_source;
        if (!geometricMatrix(properties,&geometric_source,error)) return false;
        const Animation::Mat4 geometric_canonical=canonicalMatrix(scene.basis,geometric_source);
        if (skinned) final_matrix=geometric_canonical;
        else {
            Animation::Mat4 model_global;
            if (!globalModelMatrix(scene,model_id,&model_global,error)) return false;
            final_matrix=Animation::multiply(model_global,geometric_canonical);
        }
    }

    const bool mirrored = scene.basis.mirrored != (linearDeterminant(final_matrix) < 0.0f);

    struct Builder { GeometryPart part; int slot=0; };
    std::vector<Builder> builders;
    std::unordered_map<int,std::size_t> builder_for_slot;

    for (const Polygon& polygon : polygons) {
        int slot=0;
        if (!materialSlot(materials,polygon.index,&slot,error)) return false;
        if (slot<0) return fail(error,"Geometry `"+geometry->name+"` has a negative material slot");
        if (!model_materials.empty() && static_cast<std::size_t>(slot)>=model_materials.size()) {
            return fail(error,"Geometry `"+geometry->name+"` references material slot "+std::to_string(slot)+" outside the Model material list");
        }
        std::size_t builder_index;
        if (const auto found=builder_for_slot.find(slot);found!=builder_for_slot.end()) builder_index=found->second;
        else {
            builder_index=builders.size();
            builder_for_slot.emplace(slot,builder_index);
            Builder builder;
            builder.slot=slot;
            if (!model_materials.empty()) builder.part.material=model_materials[static_cast<std::size_t>(slot)];
            builder.part.mesh.skin_inverse_bind=bind_palette;
            builders.push_back(std::move(builder));
        }
        auto& mesh=builders[builder_index].part.mesh;

        std::vector<std::array<std::size_t,3>> triangles;
        if (!triangulate(polygon,controls,&triangles,error)) return false;
        for (auto triangle : triangles) {
            if (mirrored) std::swap(triangle[1],triangle[2]);
            Models::Vec3 positions[3];
            for (int i=0;i<3;++i) positions[i]=transformPoint(final_matrix,controls[polygon.corners[triangle[static_cast<std::size_t>(i)]].control]);
            const Models::Vec3 face_normal=normalize(cross(sub(positions[1],positions[0]),sub(positions[2],positions[0])));

            for (int i=0;i<3;++i) {
                const PolygonCorner& corner=polygon.corners[triangle[static_cast<std::size_t>(i)]];
                Vertex vertex;
                vertex.position=positions[i];
                Models::Vec3 source_normal{};
                if (normals.present) {
                    if (!layerNormal(normals,polygon.index,corner,&source_normal,error)) return false;
                    const auto canonical_normal=canonicalVector(scene.basis,{source_normal.x,source_normal.y,source_normal.z});
                    vertex.normal=transformNormal(final_matrix,{canonical_normal.x,canonical_normal.y,canonical_normal.z});
                } else vertex.normal=face_normal;
                if (!layerUv(uvs,polygon.index,corner,&vertex.uv,error)) return false;
                if (corner.control<control_weights.size()) vertex.skin=control_weights[corner.control];
                const std::uint32_t index=static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
                mesh.indices.push_back(index);
            }
        }
    }

    for (auto& builder : builders) {
        if (builder.part.mesh.indices.empty()) continue;
        builder.part.mesh.bounds=calculateBounds(builder.part.mesh.vertices);
        out->push_back(std::move(builder.part));
    }
    return !out->empty() || fail(error,"Geometry `"+geometry->name+"` produced no triangles");
}

} // namespace Models::FbxInternal
