#include "fbx_fixture.hpp"

#include <cassert>
#include <cmath>
#include <string>

namespace {
using namespace HorseFbxTest;

std::string identityMatrix()
{
    return "1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1";
}

const Models::Vertex *originVertex(const Models::MeshData& mesh)
{
    for (const auto& vertex : mesh.vertices) {
        if (near(vertex.position.x,0.0f) && near(vertex.position.y,0.0f) && near(vertex.position.z,0.0f)) return &vertex;
    }
    return nullptr;
}

std::string skinScene()
{
    const std::string id = identityMatrix();
    return
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
        "    Transform: *16 { a: " + id + " }\n"
        "    TransformLink: *16 { a: " + id + " }\n"
        "  }\n"
        "  Deformer: 12, \"Deformer::ChildCluster\", \"Cluster\" {\n"
        "    Indexes: *2 { a: 0,2 }\n"
        "    Weights: *2 { a: 0.75,0.5 }\n"
        "    Transform: *16 { a: " + id + " }\n"
        "    TransformLink: *16 { a: " + id + " }\n"
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
}

void testSkinWeightsAndBindPalette()
{
    const auto document = load(writeFixture("horse-fbx-skin", skinScene()));
    assert(document.has_skeleton);
    assert(document.skeleton.bones.size() == 2u);
    assert(document.parts.size() == 1u);
    assert(document.parts[0].mesh.skin_inverse_bind.size() == 2u);
    const Models::Vertex *origin = originVertex(document.parts[0].mesh);
    assert(origin != nullptr);
    assert(near(origin->skin.weights[0], 0.75f));
    assert(near(origin->skin.weights[1], 0.25f));
    assert(origin->skin.joints[0] == 1u);
    assert(origin->skin.joints[1] == 0u);
}

void testInvalidClusterIndexFails()
{
    std::string body = skinScene();
    const std::string needle = "Indexes: *3 { a: 0,1,2 }";
    const std::size_t position = body.find(needle);
    assert(position != std::string::npos);
    body.replace(position, needle.size(), "Indexes: *3 { a: 0,1,99 }");
    Models::Fbx::Document document;
    std::string error;
    assert(!Models::Fbx::load(writeFixture("horse-fbx-skin-bad-index", body), &document, &error));
    assert(error.find("control-point") != std::string::npos);
}

void testAnimationThroughHierarchy()
{
    const std::string id = identityMatrix();
    const auto document = load(writeFixture(
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
        "}\n"));
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
    const float qlen = std::sqrt(
        rotation.x*rotation.x + rotation.y*rotation.y +
        rotation.z*rotation.z + rotation.w*rotation.w);
    assert(near(qlen, 1.0f));
}

} // namespace

int main()
{
    testSkinWeightsAndBindPalette();
    testInvalidClusterIndexFails();
    testAnimationThroughHierarchy();
    return 0;
}
