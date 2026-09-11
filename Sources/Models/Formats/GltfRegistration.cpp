#include "Models/Formats/GltfAlpha.hpp"
#include "Models/Formats/GltfAugment.hpp"
#include "Models/Formats/GltfFull.hpp"
#include "Models/Formats/Registry.hpp"

#include <string>

namespace Models::Formats {
namespace {

bool loadGltf(const std::string& path, Document *output, std::string *error)
{
    std::string local_error;
    std::string *load_error = error ? error : &local_error;
    bool loaded = GltfFull::load(path, output, load_error);
    if (!loaded && load_error->starts_with("glTF contains no mesh primitives:")) {
        load_error->clear();
        loaded = true;
    }
    return loaded &&
        GltfAugment::apply(path, output, load_error) &&
        GltfAlpha::apply(output, load_error);
}

const Registration gltf_registration(".gltf", loadGltf);
const Registration glb_registration(".glb", loadGltf);

} // namespace
} // namespace Models::Formats
