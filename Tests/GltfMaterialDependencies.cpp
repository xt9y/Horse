#include <Models/Formats/GltfMaterialSources.hpp>
#include <Models/Formats/Registry.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "horse-gltf-material-dependencies";
    std::error_code filesystem_error;
    std::filesystem::remove_all(directory, filesystem_error);
    filesystem_error.clear();
    std::filesystem::create_directories(directory, filesystem_error);
    assert(!filesystem_error);

    const std::filesystem::path bin = directory / "mesh.bin";
    {
        std::ofstream file(bin, std::ios::binary | std::ios::trunc);
        file << "abcd";
        assert(file.good());
    }

    const std::filesystem::path gltf = directory / "model.gltf";
    {
        std::ofstream file(gltf, std::ios::binary | std::ios::trunc);
        file << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":4}]})";
        assert(file.good());
    }

    Models::Formats::Document document;
    std::string error;
    assert(Models::Formats::GltfMaterialSources::apply(gltf.string(), &document, &error));
    assert(error.empty());
    assert(document.dependencies.size() == 1u);
    assert(std::filesystem::path(document.dependencies.front()).lexically_normal() ==
        std::filesystem::absolute(bin).lexically_normal());

    std::filesystem::remove_all(directory, filesystem_error);
    return 0;
}
