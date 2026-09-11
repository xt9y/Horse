#include "Models/Formats/GltfAlpha.hpp"
#include "Models/Formats/GltfAugment.hpp"
#include "Models/Formats/GltfFull.hpp"
#include "Models/Formats/Registry.hpp"

namespace Models::Formats {
namespace {

bool loadGltf(const std::string& path, Document *output, std::string *error)
{
    return GltfFull::load(path, output, error) &&
        GltfAugment::apply(path, output, error) &&
        GltfAlpha::apply(output, error);
}

const Registration gltf_registration(".gltf", loadGltf);
const Registration glb_registration(".glb", loadGltf);

} // namespace
} // namespace Models::Formats
