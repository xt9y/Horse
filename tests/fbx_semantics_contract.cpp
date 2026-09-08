#include "Models/Formats/Fbx.hpp"
#include "Models/Core/Texture.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

namespace {

bool near(float a, float b, float epsilon = 1.0e-4f)
{
    return std::abs(a - b) <= epsilon;
}

std::string settings()
{
    return
        "GlobalSettings: {\n"
        "  Properties70: {\n"
        "    P: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
        "    P: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        "    P: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "    P: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        "    P: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n"
        "    P: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        "    P: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "  }\n"
        "}\n";
}

std::string writeFixture(const char *name, const std::string& body)
{
    const std::string path = std::string("/tmp/") + name + ".fbx";
    std::ofstream file(path, std::ios::binary);
    file << "; FBX 7.4.0 project file\n" << settings() << body;
    file.close();
    return path;
}

Models::Fbx::Document load(const std::string& path)
{
    Models::Fbx::Document document;
    std::string error;
    assert(Models::Fbx::load(path, &document, &error));
    assert(error.empty());
    return document;
}

const Models::Vertex *vertexAt(const Models::MeshData& mesh, float x, float y, float z)
{
    for (const auto& vertex : mesh.vertices) {
        if (near(vertex.position.x, x) && near(vertex.position.y, y) && near(vertex.position.z, z)) {
            return &vertex;
        }
    }
    return nullptr;
}

void testConcaveNgon()
{
    const std::string path = writeFixture(
        "horse-fbx-ngon",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Concave\", \"Mesh\" {\n"
        "    Vertices: *15 { a: 0,0,0,2,0,0,2,2,0,1,1,0,0,2,0 }\n"
        "    PolygonVertexIndex: *5 { a: 0,1,2,3,-5 }\n"
        "  }\n"
        "  Model: 2, \"Model::Concave\", \"Mesh\" {}\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n"
    );
    const auto document = load(path);
    assert(document.parts.size() == 1u);
    assert(document.parts[0].mesh.indices.size() == 9u);
    assert(document.parts[0].mesh.vertices.size() == 9u);
}

void testMirroredNonUniformNormal()
{
    const std::string path = writeFixture(
        "horse-fbx-mirrored-normal",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Mirror\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "    LayerElementNormal: 0 {\n"
        "      MappingInformationType: \"ByPolygonVertex\"\n"
        "      ReferenceInformationType: \"Direct\"\n"
        "      Normals: *9 { a: 1,1,0,1,1,0,1,1,0 }\n"
        "    }\n"
        "  }\n"
        "  Model: 2, \"Model::Mirror\", \"Mesh\" {\n"
        "    Properties70: { P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",-2,1,0.5 }\n"
        "  }\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n"
    );
    const auto document = load(path);
    const auto& mesh = document.parts[0].mesh;
    assert(mesh.indices.size() == 3u);
    const float expected_x = -1.0f / std::sqrt(5.0f);
    const float expected_y = 2.0f / std::sqrt(5.0f);
    for (const auto& vertex : mesh.vertices) {
        assert(near(vertex.normal.x, expected_x));
        assert(near(vertex.normal.y, expected_y));
        assert(near(vertex.normal.z, 0.0f));
    }
    const Models::Vec3 a = mesh.vertices[mesh.indices[0]].position;
    const Models::Vec3 b = mesh.vertices[mesh.indices[1]].position;
    const Models::Vec3 c = mesh.vertices[mesh.indices[2]].position;
    const float cross_z = (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
    assert(cross_z > 0.0f);
}

void testPerPolygonMaterials()
{
    const std::string path = writeFixture(
        "horse-fbx-material-slots",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::TwoParts\", \"Mesh\" {\n"
        "    Vertices: *12 { a: 0,0,0,1,0,0,1,1,0,0,1,0 }\n"
        "    PolygonVertexIndex: *6 { a: 0,1,-3,0,2,-4 }\n"
        "    LayerElementMaterial: 0 {\n"
        "      MappingInformationType: \"ByPolygon\"\n"
        "      ReferenceInformationType: \"IndexToDirect\"\n"
        "      Materials: *2 { a: 0,1 }\n"
        "    }\n"
        "  }\n"
        "  Model: 2, \"Model::TwoParts\", \"Mesh\" {}\n"
        "  Material: 10, \"Material::Red\", \"\" {\n"
        "    Properties70: { P: \"DiffuseColor\", \"Color\", \"\", \"A\",1,0,0 }\n"
        "  }\n"
        "  Material: 11, \"Material::Green\", \"\" {\n"
        "    Properties70: { P: \"DiffuseColor\", \"Color\", \"\", \"A\",0,1,0 }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "  C: \"OO\",10,2\n"
        "  C: \"OO\",11,2\n"
        "}\n"
    );
    const auto document = load(path);
    assert(document.parts.size() == 2u);
    assert(document.parts[0].mesh.indices.size() == 3u);
    assert(document.parts[1].mesh.indices.size() == 3u);
    assert(document.parts[0].material.name == "Red");
    assert(document.parts[1].material.name == "Green");
}

void testInvalidLayerFails()
{
    const std::string path = writeFixture(
        "horse-fbx-invalid-layer",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::BadUv\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "    LayerElementUV: 0 {\n"
        "      MappingInformationType: \"ByPolygonVertex\"\n"
        "      ReferenceInformationType: \"IndexToDirect\"\n"
        "      UV: *4 { a: 0,0,1,1 }\n"
        "      UVIndex: *3 { a: 0,9,0 }\n"
        "    }\n"
        "  }\n"
        "  Model: 2, \"Model::BadUv\", \"Mesh\" {}\n"
        "}\n"
        "Connections: { C: \"OO\",1,2 }\n"
    );
    Models::Fbx::Document document;
    std::string error;
    assert(!Models::Fbx::load(path, &document, &error));
    assert(!error.empty());
    assert(error.find("UV") != std::string::npos);
}

void writeTinyTga(const std::string& path)
{
    const std::uint8_t bytes[] = {
        0,0,2, 0,0,0,0,0, 0,0,0,0,
        1,0,1,0,24,0x20,
        0,0,255
    };
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

std::string texturedScene(const std::string& filename)
{
    return
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Textured\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::Textured\", \"Mesh\" {}\n"
        "  Material: 10, \"Material::Surface\", \"\" {\n"
        "    Properties70: { P: \"DiffuseColor\", \"Color\", \"\", \"A\",1,1,1 }\n"
        "  }\n"
        "  Texture: 11, \"Texture::Diffuse\", \"\" { RelativeFilename: \"" + filename + "\" }\n"
        "  Video: 12, \"Video::Diffuse\", \"Clip\" { Filename: \"" + filename + "\" }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "  C: \"OO\",10,2\n"
        "  C: \"OP\",11,10,\"DiffuseColor\"\n"
        "  C: \"OO\",12,11\n"
        "}\n";
}

void testExternalTextureGraph()
{
    Models::clearTextureCache();
    const std::string texture_path = "/tmp/horse-fbx-diffuse.tga";
    writeTinyTga(texture_path);
    const auto document = load(writeFixture("horse-fbx-external-texture", texturedScene("horse-fbx-diffuse.tga")));
    assert(document.parts.size() == 1u);
    assert(document.parts[0].material.diffuse_texture != Models::INVALID_TEXTURE);
    assert(document.parts[0].material.texture_path == texture_path);
}

void testExistingBadTextureFails()
{
    Models::clearTextureCache();
    const std::string bad_path = "/tmp/horse-fbx-bad-image.bin";
    {
        std::ofstream file(bad_path, std::ios::binary);
        file << "not-an-image";
    }
    const std::string path = writeFixture("horse-fbx-bad-texture", texturedScene("horse-fbx-bad-image.bin"));
    Models::Fbx::Document document;
    std::string error;
    assert(!Models::Fbx::load(path, &document, &error));
    assert(!error.empty());
    assert(error.find("decode") != std::string::npos || error.find("image") != std::string::npos);
}

float maxX(const Models::MeshData& mesh)
{
    float value = -1.0e30f;
    for (const auto& vertex : mesh.vertices) value = std::max(value, vertex.position.x);
    return value;
}

void testInheritanceModes()
{
    for (int mode = 0; mode <= 2; ++mode) {
        const std::string path = writeFixture(
            (std::string("horse-fbx-inherit-") + std::to_string(mode)).c_str(),
            "Objects: {\n"
            "  Geometry: 1, \"Geometry::Child\", \"Mesh\" {\n"
            "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
            "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
            "  }\n"
            "  Model: 2, \"Model::Parent\", \"Null\" {\n"
            "    Properties70: { P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",2,2,2 }\n"
            "  }\n"
            "  Model: 3, \"Model::Child\", \"Mesh\" {\n"
            "    Properties70: {\n"
            "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0\n"
            "      P: \"InheritType\", \"enum\", \"\", \"\"," + std::to_string(mode) + "\n"
            "    }\n"
            "  }\n"
            "}\n"
            "Connections: {\n"
            "  C: \"OO\",1,3\n"
            "  C: \"OO\",3,2\n"
            "}\n"
        );
        const auto document = load(path);
        const float expected = mode == 2 ? 3.0f : 4.0f;
        assert(near(maxX(document.parts[0].mesh), expected));
    }
}

void testGeometricTransformDoesNotInherit()
{
    const std::string path = writeFixture(
        "horse-fbx-geometric-parent",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Child\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::Parent\", \"Null\" {\n"
        "    Properties70: { P: \"GeometricTranslation\", \"Vector3D\", \"Vector\", \"\",10,0,0 }\n"
        "  }\n"
        "  Model: 3, \"Model::Child\", \"Mesh\" {\n"
        "    Properties70: { P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0 }\n"
        "  }\n"
        "}\n"
        "Connections: { C: \"OO\",1,3 C: \"OO\",3,2 }\n"
    );
    const auto document = load(path);
    assert(near(maxX(document.parts[0].mesh), 2.0f));
}

std::string identityMatrix()
{
    return "1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1";
}

void testSkinWeightsAndInvalidCluster()
{
    const std::string common =
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::SkinMesh\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::SkinMesh\", \"Mesh\" {}\n"
        "  Model: 20, \"Model::RootBone\", \"LimbNode\" {}\n"
        "  Model: 21, \"Model::ChildBone\", \"LimbNode\" {\n"
        "    Properties70: { P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0 }\n"
        "  }\n"
        "  Deformer: 10, \"Deformer::Skin\", \"Skin\" {}\n"
        "  Deformer: 11, \"Deformer::RootCluster\", \"Cluster\" {\n"
        "    Indexes: *3 { a: 0,1,2 }\n"
        "    Weights: *3 { a: 0.25,1,0.5 }\n"
        "    Transform: *16 { a: " + identityMatrix() + " }\n"
        "    TransformLink: *16 { a: " + identityMatrix() + " }\n"
        "  }\n"
        "  Deformer: 12, \"Deformer::ChildCluster\", \"Cluster\" {\n"
        "    Indexes: *2 { a: 0,2 }\n"
        "    Weights: *2 { a: 0.75,0.5 }\n"
        "    Transform: *16 { a: " + identityMatrix() + " }\n"
        "    TransformLink: *16 { a: " + identityMatrix() + " }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "  C: \"OO\",10,1\n"
        "  C: \"OO\",11,10\n"
        "  C: \"OO\",12,10\n"
        "  C: \"OO\",20,11\n"
        "  C: \"OO\",21,12\n"
        "  C: \"OO\",21,20\n"
        "}\n";
    const auto document = load(writeFixture("horse-fbx-skin", common));
    assert(document.has_skeleton);
    assert(document.skeleton.bones.size() == 2u);
    assert(document.parts[0].mesh.skin_inverse_bind.size() == 2u);
    const Models::Vertex *origin = vertexAt(document.parts[0].mesh, 0.0f, 0.0f, 0.0f);
    assert(origin != nullptr);
    assert(near(origin->skin.weights[0], 0.75f));
    assert(near(origin->skin.weights[1], 0.25f));
    assert(origin->skin.joints[0] == 1u);
    assert(origin->skin.joints[1] == 0u);

    std::string bad = common;
    const std::string needle = "Indexes: *3 { a: 0,1,2 }";
    const std::size_t at = bad.find(needle);
    assert(at != std::string::npos);
    bad.replace(at, needle.size(), "Indexes: *3 { a: 0,1,99 }");
    Models::Fbx::Document rejected;
    std::string error;
    assert(!Models::Fbx::load(writeFixture("horse-fbx-skin-bad-index", bad), &rejected, &error));
    assert(error.find("control-point") != std::string::npos);
}

void testAnimationThroughHierarchy()
{
    const std::string id = identityMatrix();
    const std::string path = writeFixture(
        "horse-fbx-animation",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Animated\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::Animated\", \"Mesh\" {}\n"
        "  Model: 20, \"Model::ParentBone\", \"LimbNode\" {\n"
        "    Properties70: {\n"
        "      P: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",0,0,30\n"
        "      P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",2,2,2\n"
        "    }\n"
        "  }\n"
        "  Model: 21, \"Model::ChildBone\", \"LimbNode\" {\n"
        "    Properties70: {\n"
        "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",1,0,0\n"
        "      P: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",10,20,30\n"
        "      P: \"RotationOrder\", \"enum\", \"\", \"\",5\n"
        "    }\n"
        "  }\n"
        "  Deformer: 10, \"Deformer::Skin\", \"Skin\" {}\n"
        "  Deformer: 11, \"Deformer::Cluster\", \"Cluster\" {\n"
        "    Indexes: *3 { a: 0,1,2 }\n"
        "    Weights: *3 { a: 1,1,1 }\n"
        "    Transform: *16 { a: " + id + " }\n"
        "    TransformLink: *16 { a: " + id + " }\n"
        "  }\n"
        "  AnimationStack: 30, \"AnimStack::Move\", \"\" {}\n"
        "  AnimationLayer: 31, \"AnimLayer::Base\", \"\" {}\n"
        "  AnimationCurveNode: 40, \"AnimCurveNode::T\", \"\" {}\n"
        "  AnimationCurve: 41, \"AnimCurve::X\", \"\" {\n"
        "    KeyTime: *2 { a: 0,46186158000 }\n"
        "    KeyValueFloat: *2 { a: 1,3 }\n"
        "  }\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "  C: \"OO\",10,1\n"
        "  C: \"OO\",11,10\n"
        "  C: \"OO\",21,11\n"
        "  C: \"OO\",21,20\n"
        "  C: \"OO\",31,30\n"
        "  C: \"OO\",40,31\n"
        "  C: \"OP\",40,21,\"Lcl Translation\"\n"
        "  C: \"OP\",41,40,\"d|X\"\n"
        "}\n"
    );
    const auto document = load(path);
    assert(document.has_skeleton);
    assert(document.skeleton.bones.size() == 2u);
    assert(document.animations.size() == 1u);
    const auto& clip = document.animations[0];
    assert(near(clip.duration, 1.0f));
    assert(clip.tracks.size() == 2u);
    assert(clip.tracks[1].samples.size() >= 2u);
    assert(near(clip.tracks[1].samples.front().translation.x, 1.0f));
    assert(near(clip.tracks[1].samples.back().translation.x, 3.0f));
    const auto& rotation = clip.tracks[1].samples.back().rotation;
    const float qlen = std::sqrt(rotation.x*rotation.x + rotation.y*rotation.y + rotation.z*rotation.z + rotation.w*rotation.w);
    assert(near(qlen, 1.0f));
}

} // namespace

int main()
{
    testConcaveNgon();
    testMirroredNonUniformNormal();
    testPerPolygonMaterials();
    testInvalidLayerFails();
    testExternalTextureGraph();
    testExistingBadTextureFails();
    testInheritanceModes();
    testGeometricTransformDoesNotInherit();
    testSkinWeightsAndInvalidCluster();
    testAnimationThroughHierarchy();
    return 0;
}
