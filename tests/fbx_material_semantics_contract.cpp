#include "fbx_fixture.hpp"
#include "Models/Core/Texture.hpp"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>

namespace {
using namespace HorseFbxTest;

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
        "  Texture: 11, \"Texture::Diffuse\", \"\" {\n"
        "    RelativeFilename: \"" + filename + "\"\n"
        "  }\n"
        "  Video: 12, \"Video::Diffuse\", \"Clip\" {\n"
        "    Filename: \"" + filename + "\"\n"
        "  }\n"
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
    const auto document = load(writeFixture(
        "horse-fbx-external-texture",
        texturedScene("horse-fbx-diffuse.tga")));
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
    const std::string path = writeFixture(
        "horse-fbx-bad-texture",
        texturedScene("horse-fbx-bad-image.bin"));
    Models::Fbx::Document document;
    std::string error;
    assert(!Models::Fbx::load(path, &document, &error));
    assert(!error.empty());
    assert(error.find("decode") != std::string::npos || error.find("image") != std::string::npos);
}

void testMissingTextureReferenceStaysTextureless()
{
    Models::clearTextureCache();
    const auto document = load(writeFixture(
        "horse-fbx-no-texture",
        "Objects: {\n"
        "  Geometry: 1, \"Geometry::Plain\", \"Mesh\" {\n"
        "    Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n"
        "    PolygonVertexIndex: *3 { a: 0,1,-3 }\n"
        "  }\n"
        "  Model: 2, \"Model::Plain\", \"Mesh\" {}\n"
        "  Material: 10, \"Material::Plain\", \"\" {}\n"
        "}\n"
        "Connections: {\n"
        "  C: \"OO\",1,2\n"
        "  C: \"OO\",10,2\n"
        "}\n"));
    assert(document.parts.size() == 1u);
    assert(document.parts[0].material.diffuse_texture == Models::INVALID_TEXTURE);
    assert(document.parts[0].material.texture_path.empty());
}

} // namespace

int main()
{
    testExternalTextureGraph();
    testExistingBadTextureFails();
    testMissingTextureReferenceStaysTextureless();
    return 0;
}
